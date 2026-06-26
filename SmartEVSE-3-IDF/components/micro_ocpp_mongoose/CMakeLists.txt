# =============================================================================
# MicroOcppMongoose — Mongoose transport adapter for MicroOcpp
#
# Source: https://github.com/matth-x/MicroOcppMongoose (branch compatible
# with MicroOcpp 942fb88a).
# =============================================================================

idf_component_register(
    SRC_DIRS
        "src"
    INCLUDE_DIRS
        "src"
    REQUIRES
        micro_ocpp
        mongoose
        arduino_compat
)

if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/src/MicroOcppMongooseClient.h")
    message(FATAL_ERROR
        "MicroOcppMongoose sources not found in components/micro_ocpp_mongoose/src/. "
        "Clone matth-x/MicroOcppMongoose and copy src/ here.")
endif()

# Tell MicroOcppMongooseClient.cpp which Mongoose version we are
# linking against. MO_MG_V720 selects the API for Mongoose 7.20+:
# the 3-argument event handler signature (fn_data read from
# c->fn_data) and the mg_str-based struct fields (mg_ws_message.data
# .buf, mg_tls_opts.ca/.name, ...). We currently use Mongoose 7.21
# (see components/mongoose/CMakeLists.txt) but MicroOcpp 1.7.x
# does not define a V721 constant, so V720 is the correct value.
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    MO_MG_USE_VERSION=MO_MG_V720
)
