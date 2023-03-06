cmake_minimum_required(VERSION 3.13)
project(slcl)
add_executable(${PROJECT_NAME}
    auth.c
    base64.c
    cftw.c
    handler.c
    html.c
    http.c
    jwt.c
    main.c
    page.c
    server.c
)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall)
target_compile_definitions(${PROJECT_NAME} PRIVATE _FILE_OFFSET_BITS=64)
add_subdirectory(dynstr)
find_package(cJSON 1.0 REQUIRED)
find_package(OpenSSL 3.0 REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE dynstr cjson OpenSSL::SSL)
