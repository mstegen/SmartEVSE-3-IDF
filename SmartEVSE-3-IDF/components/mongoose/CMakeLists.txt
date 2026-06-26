# =============================================================================
# Mongoose 7.21 — vendored single-file component
#
# The file mongoose.c and header mongoose.h in this component dir are the
# same copies kept under src/mongoose.{c,h}. They are kept in sync manually
# (run `cp src/mongoose.{c,h} components/mongoose/` after updating either
# copy, since the build compiles the file in this component dir).
#
# Mongoose 7.21 is the first version that compiles cleanly against mbedTLS
# 4.0 (which IDF v6 ships). It uses the new mbedTLS 4.0 API:
#   - mbedtls_ssl_ticket_setup(..., PSA_ALG_GCM, PSA_KEY_TYPE_AES, ...)
#   - mbedtls_ssl_conf_rng(...)   (unchanged)
#   - mbedtls_pk_parse_key(ctx, key, keylen, pwd, pwdlen)  (legacy shim)
#
# MO_MG_USE_VERSION=MO_MG_V720 selects MicroOcpp's MG 7.20+ code path
# (3-arg event handler, mg_str-based ws/tls structs, mg_ws_connect with
#  the new TLS field names). MicroOcpp 1.7.x does not define a V721
#  constant, so V720 is the right value for mongoose 7.21.
# =============================================================================

idf_component_register(
    SRC_DIRS
        "."
    INCLUDE_DIRS
        "."
    REQUIRES app_update esp_timer mbedtls
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    MG_ENABLE_PACKED_FS=1
    MG_TLS=MG_TLS_MBED
    MG_ARCH=MG_ARCH_ESP32
    MG_ENABLE_LOG=0
    MO_MG_USE_VERSION=MO_MG_V720
)

# Make sure the public include is reachable from the `app` component
target_include_directories(${COMPONENT_LIB} PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}"
)
