idf_component_register(SRC_DIRS          "."
                      PRIV_INCLUDE_DIRS  ".")

set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")

set(ulp_app_name lp_core_${COMPONENT_NAME})
set(ulp_lp_core_sources "lp_core/main.c")
set(ulp_exp_dep_srcs "app_main.cpp")
ulp_embed_binary(${ulp_app_name} "${ulp_lp_core_sources}" "${ulp_exp_dep_srcs}")