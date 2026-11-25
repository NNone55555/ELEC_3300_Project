set(security_dir "${CMAKE_CURRENT_LIST_DIR}")
set(bearssl "${security_dir}/third_party/bearssl")
if(NOT EXISTS "${security_dir}/private/radio_key.h")
    message(FATAL_ERROR "Run python tools/provision_keys.py from the repository root before building either board.")
endif()
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    "${bearssl}/inc" "${bearssl}/src" "${security_dir}/private")
set(bearssl_sources
    "${bearssl}/src/aead/ccm.c"
    "${bearssl}/src/symcipher/aes_ct.c"
    "${bearssl}/src/symcipher/aes_ct_enc.c"
    "${bearssl}/src/symcipher/aes_ct_ctrcbc.c"
)
# Keep the radio's CPU and stack use bounded even in the firmware Debug build.
set_source_files_properties(${bearssl_sources} PROPERTIES COMPILE_OPTIONS "-O2")
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    "${security_dir}/aes_ccm.c"
    "${security_dir}/secure_radio.c"
    "${security_dir}/boot_epoch.c"
    ${bearssl_sources}
)
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "LINKER:-T,${security_dir}/nonce_guard.ld")
set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS "${security_dir}/nonce_guard.ld")
