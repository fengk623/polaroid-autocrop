# fkcli — command-line front end over the core API (RFC-0001): integration tests, eval, measurements.
add_executable(fkcli main.cpp common.cpp eval.cpp)
target_link_libraries(fkcli PRIVATE fk::core nlohmann_json::nlohmann_json)
fk_set_warnings(fkcli)
