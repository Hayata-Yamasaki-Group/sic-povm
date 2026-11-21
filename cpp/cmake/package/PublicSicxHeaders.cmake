# SPDX-License-Identifier: AGPL-3.0-or-later
# Explicit consumer dependency closure. No producer/source-identity interface.
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/error.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/support/ball_vectors.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/support")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/limits.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/sicx/format.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/sicx")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/sicx/branches.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/sicx")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/decode/decoder.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/decode")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/sicx/exact.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/sicx")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/sicx/finalize.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/sicx")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/sicx/io.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/sicx")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/sicx/json.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/sicx")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/access/remote.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/access")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/sicx/rescaling_rule.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/sicx")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/sicx/verify.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/sicx")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/support/admission.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/support")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/support/cli.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/support")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/support/durable_file.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/support")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/support/flint_raii.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/support")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/support/json.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/support")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/support/stop.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/support")
install(FILES "${PROJECT_SOURCE_DIR}/include/sic/support/thread_group.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/support")


install(FILES "${PROJECT_SOURCE_DIR}/include/sic/support/kernel_settings.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/support")

install(FILES "${PROJECT_SOURCE_DIR}/include/sic/transport/parts.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/transport")

install(FILES "${PROJECT_SOURCE_DIR}/include/sic/decode/placement.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sic/decode")
