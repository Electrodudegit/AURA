#define MBEDTLS_ALLOW_PRIVATE_ACCESS
/* tls_probe.c - AURA v3.1.3 TLS handshake heap probe (host, -m32).
 * Mirrors arduino-esp32 3.3.12 ssl_client.cpp start_ssl_client() + the ESP-IDF CA-bundle
 * verify path, using mbedTLS 3.6.6 built with the prebuilt esp_config.h + sdkconfig.h
 * (hardware ALT hooks removed). Handshake ONLY: no HTTP request, no API key is used or read.
 * Every mbedTLS calloc/free is tracked: payload bytes and an ESP heap estimate per block
 * (TLSF 4 B header + HEAP_POISONING_LIGHT 8 B head + 4 B tail canary, 4-byte aligned). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mbedtls/platform.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/error.h"
#include "../mbedtls-3.6.6/library/ssl_misc.h"

typedef struct { uint32_t sz; uint32_t pad; } hdr_t;
static long cur, peak, cur_esp, peak_esp, nblk, peak_nblk, big1, big2, nalloc;
static long max_single;
static int phase; static long phase_peak_esp[8], phase_peak[8];
static long esp_cost(uint32_t n) { return (long)(((n + 12u + 3u) & ~3u) + 4u); }
static void *t_calloc(size_t n, size_t s) {
  size_t b = n * s; hdr_t *h = calloc(1, sizeof(hdr_t) + b); if (!h) return NULL;
  h->sz = (uint32_t)b; cur += b; cur_esp += esp_cost(h->sz); nblk++; nalloc++;
  if ((long)b > max_single) max_single = b;
  if (cur > peak) peak = cur;
  if (cur_esp > peak_esp) { peak_esp = cur_esp; peak_nblk = nblk; }
  if (cur > phase_peak[phase]) phase_peak[phase] = cur;
  if (cur_esp > phase_peak_esp[phase]) phase_peak_esp[phase] = cur_esp;
  if ((long)b >= 4096) fprintf(stderr, "  big alloc %zu (phase %d)\n", b, phase);
  return h + 1;
}
static void t_free(void *p) {
  if (!p) return; hdr_t *h = ((hdr_t *)p) - 1; cur -= h->sz; cur_esp -= esp_cost(h->sz); nblk--; free(h);
}

/* ESP-IDF crt bundle emulation: the bundle stores root public keys; at the top of the chain the
 * verify callback parses the root key into a temporary pk context and checks the child signature. */
static mbedtls_x509_crt s_roots;  /* host-side root store, loaded before the measurement (= flash on ESP) */
static int bundle_verify_cb(void *u, mbedtls_x509_crt *crt, int depth, uint32_t *flags) {
  (void)u; (void)depth;
  if (!(*flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED)) return 0;
  for (mbedtls_x509_crt *r = &s_roots; r && r->raw.len; r = r->next) {
    if (crt->issuer_raw.len != r->subject_raw.len || memcmp(crt->issuer_raw.p, r->subject_raw.p, crt->issuer_raw.len)) continue;
    unsigned char key[1200]; unsigned char *e = key + sizeof(key);
    int kl = mbedtls_pk_write_pubkey_der(&r->pk, key, sizeof(key));  /* uses no heap for RSA/EC */
    if (kl <= 0) continue;
    mbedtls_pk_context pk; mbedtls_pk_init(&pk);
    int ret = mbedtls_pk_parse_public_key(&pk, e - kl, (size_t)kl);
    unsigned char hash[64]; const mbedtls_md_info_t *mi = mbedtls_md_info_from_type(crt->sig_md);
    if (!ret && mi) {
      mbedtls_md(mi, crt->tbs.p, crt->tbs.len, hash);
      ret = mbedtls_pk_verify_ext(crt->sig_pk, crt->sig_opts, &pk, crt->sig_md, hash, mbedtls_md_get_size(mi), crt->sig.p, crt->sig.len);
    }
    mbedtls_pk_free(&pk);
    if (!ret) { *flags = 0; fprintf(stderr, "  bundle root match at depth %d, key %d B\n", depth, kl); return 0; }
  }
  return 0;
}

int main(int argc, char **argv) {
  const char *host = argc > 1 ? argv[1] : "api.groq.com";
  const char *ca = argc > 2 ? argv[2] : "/etc/ssl/certs/ca-certificates.crt";
  mbedtls_x509_crt_init(&s_roots);
  if (mbedtls_x509_crt_parse_file(&s_roots, ca) < 0) { fprintf(stderr, "ca load failed\n"); return 2; }
  mbedtls_platform_set_calloc_free(t_calloc, t_free);  /* measurement starts here */
  mbedtls_ssl_context ssl; mbedtls_ssl_config conf; mbedtls_ctr_drbg_context drbg; mbedtls_entropy_context ent;
  mbedtls_net_context net; mbedtls_x509_crt dummy;
  mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf); mbedtls_ctr_drbg_init(&drbg); mbedtls_net_init(&net);
  mbedtls_x509_crt_init(&dummy);
  phase = 0;
  if (mbedtls_net_connect(&net, host, "443", MBEDTLS_NET_PROTO_TCP)) { fprintf(stderr, "tcp connect failed\n"); return 3; }
  mbedtls_entropy_init(&ent);
  const char *pers = "esp32-tls";
  int ret = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &ent, (const unsigned char *)pers, strlen(pers));
  ret |= mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
  /* esp_crt_bundle_attach: dummy CA chain + verify callback, VERIFY_REQUIRED */
  mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_ca_chain(&conf, &dummy, NULL);
  mbedtls_ssl_conf_verify(&conf, bundle_verify_cb, NULL);
  ret |= mbedtls_ssl_set_hostname(&ssl, host);
  mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
  long before_setup = cur_esp;
  phase = 1;
  ret |= mbedtls_ssl_setup(&ssl, &conf);
  long after_setup = cur_esp, after_setup_pl = cur;
  if (ret) { fprintf(stderr, "setup failed %d\n", ret); return 4; }
  mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv, NULL);
  phase = 2;
  while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
      char e[128]; mbedtls_strerror(ret, e, sizeof e); fprintf(stderr, "handshake failed: %s\n", e); return 5;
    }
  }
  phase = 3;
  long after_hs = cur_esp, after_hs_pl = cur, after_hs_blk = nblk;
  const mbedtls_x509_crt *pc = mbedtls_ssl_get_peer_cert(&ssl);
  int nchain = 0; size_t chain_raw = 0;
  for (const mbedtls_x509_crt *c = pc; c; c = c->next) { nchain++; chain_raw += c->raw.len; }
  printf("host=%s proto=%s suite=%s verify=0x%x\n", host, mbedtls_ssl_get_version(&ssl), mbedtls_ssl_get_ciphersuite(&ssl),
         (unsigned)mbedtls_ssl_get_verify_result(&ssl));
  printf("peer_chain certs=%d raw=%zu B  pk=%s %zu bits\n", nchain, chain_raw, pc ? mbedtls_pk_get_name(&pc->pk) : "-",
         pc ? mbedtls_pk_get_bitlen(&pc->pk) : 0);
  printf("IN_BUF=%d OUT_BUF=%d (content %d/%d)\n", (int)MBEDTLS_SSL_IN_BUFFER_LEN, (int)MBEDTLS_SSL_OUT_BUFFER_LEN,
         (int)MBEDTLS_SSL_IN_CONTENT_LEN, (int)MBEDTLS_SSL_OUT_CONTENT_LEN);
  printf("pre_setup esp=%ld\n", before_setup);
  printf("after_setup payload=%ld esp=%ld\n", after_setup_pl, after_setup);
  printf("handshake_peak payload=%ld esp=%ld blocks_at_peak=%ld largest_single=%ld allocs=%ld\n", phase_peak[2] > phase_peak[1] ? phase_peak[2] : phase_peak[1],
         phase_peak_esp[2] > phase_peak_esp[1] ? phase_peak_esp[2] : phase_peak_esp[1], peak_nblk, max_single, nalloc);
  printf("session_steady payload=%ld esp=%ld blocks=%ld\n", after_hs_pl, after_hs, after_hs_blk);
  mbedtls_ssl_close_notify(&ssl);
  mbedtls_net_free(&net); mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&ent);
  printf("after_free payload=%ld esp=%ld blocks=%ld\n", cur, cur_esp, nblk);
  return 0;
}
