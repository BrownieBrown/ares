cmake_minimum_required(VERSION 3.25)
project(ares
    VERSION 0.1.0
    DESCRIPTION "Personal Financial Management Application"
    LANGUAGES CXX
)

# ============================================================
# Project-wide settings
# ============================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type defaults
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# ============================================================
# Options
# ============================================================
option(ARES_BUILD_TESTS "Build unit and integration tests" ON)
option(ARES_USE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

# ============================================================
# Dependencies
# ============================================================
include(FetchContent)

# SQLite
find_package(SQLite3 REQUIRED)

# Catch2 for testing
if(ARES_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.2
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

# fmt library (for formatting until std::format is fully portable)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.2.1
)
FetchContent_MakeAvailable(fmt)

# CLI11 for command-line parsing
FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.1
)
FetchContent_MakeAvailable(CLI11)

# ============================================================
# Compiler warnings
# ============================================================
add_library(ares_warnings INTERFACE)
target_compile_options(ares_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wcast-qual
    -Wformat=2
    -Wundef
    -Werror=return-type
    -Wshadow
    -Wcast-align
    -Wunused
    -Wnull-dereference
)

if(ARES_USE_SANITIZERS)
    add_library(ares_sanitizers INTERFACE)
    target_compile_options(ares_sanitizers INTERFACE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(ares_sanitizers INTERFACE
        -fsanitize=address,undefined
    )
endif()

# ============================================================
# Core library (domain logic)
# ============================================================
add_library(ares_core STATIC
    src/core/common/Money.cpp
    src/core/common/Error.cpp
    src/core/account/Account.cpp
    src/core/transaction/Transaction.cpp
    src/core/transaction/RecurringPattern.cpp
    src/core/transaction/Adjustment.cpp
    src/core/credit/Credit.cpp
)
target_include_directories(ares_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ares_core
    PUBLIC fmt::fmt
    PRIVATE ares_warnings
)

# ============================================================
# Infrastructure library (persistence, import/export)
# ============================================================
add_library(ares_infrastructure STATIC
    src/infrastructure/import/CsvParser.cpp
    src/infrastructure/import/IngDeCsvImporter.cpp
    src/infrastructure/config/ConfigParser.cpp
    src/infrastructure/persistence/DatabaseConnection.cpp
    src/infrastructure/persistence/SqliteTransactionRepository.cpp
    src/infrastructure/persistence/SqliteAccountRepository.cpp
    src/infrastructure/persistence/SqliteCreditRepository.cpp
    src/infrastructure/persistence/SqliteRecurringPatternRepository.cpp
    src/infrastructure/persistence/SqliteAdjustmentRepository.cpp
    src/infrastructure/persistence/AccountRepository.cpp
    src/infrastructure/persistence/TransactionRepository.cpp
    src/infrastructure/persistence/CreditRepository.cpp
)
target_include_directories(ares_infrastructure PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ares_infrastructure
    PUBLIC ares_core SQLite::SQLite3
    PRIVATE ares_warnings
)

# ============================================================
# Application library (services, use cases)
# ============================================================
add_library(ares_application STATIC
    src/application/services/AccountService.cpp
    src/application/services/TransactionService.cpp
    src/application/services/CreditService.cpp
    src/application/services/ImportService.cpp
    src/application/services/BudgetService.cpp
    src/application/services/RecurrenceDetector.cpp
    src/application/services/ConfigService.cpp
)
target_include_directories(ares_application PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ares_application
    PUBLIC ares_core ares_infrastructure
    PRIVATE ares_warnings
)

# ============================================================
# Main executable
# ============================================================
add_executable(ares
    src/main.cpp
    src/presentation/cli/CliApp.cpp
)
target_link_libraries(ares
    PRIVATE
        ares_application
        CLI11::CLI11
        ares_warnings
)

if(ARES_USE_SANITIZERS)
    target_link_libraries(ares PRIVATE ares_sanitizers)
endif()

# ============================================================
# Tests
# ============================================================
if(ARES_BUILD_TESTS)
    enable_testing()
    include(CTest)
    include(Catch)

    # Unit tests
    add_executable(ares_unit_tests
        tests/unit/core/MoneyTests.cpp
        tests/unit/core/AccountTests.cpp
        tests/unit/core/TransactionTests.cpp
        tests/unit/infrastructure/IngDeCsvImporterTests.cpp
        tests/unit/infrastructure/ConfigParserTests.cpp
        tests/unit/services/RecurrenceDetectorTests.cpp
        tests/unit/services/BudgetServiceTests.cpp
    )
    target_link_libraries(ares_unit_tests
        PRIVATE
            ares_core
            ares_infrastructure
            ares_application
            Catch2::Catch2WithMain
            ares_warnings
    )

    # Register tests with CTest
    catch_discover_tests(ares_unit_tests)
endif()

# ============================================================
# Installation
# ============================================================
install(TARGETS ares RUNTIME DESTINATION bin)
