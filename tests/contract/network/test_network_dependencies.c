/* Actual pinned TLS and Argon2id libraries; no server or credentials fixture. */
#include <string.h>
#include <sodium.h>
#include <openssl/opensslv.h>
#include <openssl/ssl.h>
#include "civetweb.h"
#include "test_support.h"

int main(void)
{
    char hash[crypto_pwhash_STRBYTES];
    const char password[] = "formal-regression-password";
    TEST_CHECK(sodium_init() >= 0);
    TEST_CHECK(strcmp(sodium_version_string(), "1.0.22") == 0);
    TEST_CHECK(OPENSSL_VERSION_MAJOR == 3 && OPENSSL_VERSION_MINOR == 5);
    TEST_CHECK(strstr(OpenSSL_version(OPENSSL_VERSION), "3.5.9") != NULL);
    TEST_CHECK(crypto_pwhash_str_alg(hash, password, sizeof(password) - 1u,
        crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
        crypto_pwhash_ALG_ARGON2ID13) == 0);
    TEST_CHECK(strncmp(hash, "$argon2id$", 10u) == 0);
    TEST_CHECK(crypto_pwhash_str_verify(hash, password, sizeof(password) - 1u) == 0);
    TEST_CHECK(crypto_pwhash_str_verify(hash, "wrong-password", 14u) != 0);
    TEST_CHECK(strcmp(mg_version(), "1.16") == 0);
    TEST_CHECK((mg_init_library(MG_FEATURES_TLS | MG_FEATURES_WEBSOCKET)
                & MG_FEATURES_TLS) != 0u);
    mg_exit_library();
    sodium_memzero(hash, sizeof(hash));
    TEST_REPORT("network.pinned_dependencies");
}
