cmake_minimum_required(VERSION 3.20)

set(NIR_TD_DIR ${PROJECT_SOURCE_DIR}/mlir)

set(LLVM_TARGET_DEFINITIONS ${NIR_TD_DIR}/NarvalDialect.td)
mlir_tablegen(NarvalDialect.h.inc   -gen-dialect-decls   -I ${NIR_TD_DIR})
mlir_tablegen(NarvalDialect.cpp.inc -gen-dialect-defs    -I ${NIR_TD_DIR})
add_public_tablegen_target(NarvalDialectIncGen)

set(LLVM_TARGET_DEFINITIONS ${NIR_TD_DIR}/NarvalTypes.td)
mlir_tablegen(NarvalTypes.h.inc   -gen-typedef-decls --typedefs-dialect=narval -I ${NIR_TD_DIR})
mlir_tablegen(NarvalTypes.cpp.inc -gen-typedef-defs  --typedefs-dialect=narval -I ${NIR_TD_DIR})
add_public_tablegen_target(NarvalTypesIncGen)

set(LLVM_TARGET_DEFINITIONS ${NIR_TD_DIR}/NarvalOps.td)
mlir_tablegen(NarvalOps.h.inc   -gen-op-decls --dialect=narval -I ${NIR_TD_DIR})
mlir_tablegen(NarvalOps.cpp.inc -gen-op-defs  --dialect=narval -I ${NIR_TD_DIR})
add_public_tablegen_target(NarvalOpsIncGen)

set(LLVM_TARGET_DEFINITIONS ${NIR_TD_DIR}/NarvalPasses.td)
mlir_tablegen(NarvalPasses.h.inc -gen-pass-decls --name Narval -I ${NIR_TD_DIR})
add_public_tablegen_target(NarvalPassesIncGen)

file(GLOB_RECURSE NIR_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(NarvalNirDialect ${NIR_SOURCES})

target_compile_definitions(NarvalNirDialect PRIVATE NARVAL_USE_NIR=1)

add_dependencies(NarvalNirDialect
    NarvalDialectIncGen
    NarvalTypesIncGen
    NarvalOpsIncGen
    NarvalPassesIncGen
)

target_include_directories(NarvalNirDialect
    PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}
        ${PROJECT_SOURCE_DIR}/include
        ${MLIR_INCLUDE_DIRS}
        ${LLVM_INCLUDE_DIRS}
)

set(MLIR_DIALECT_LIBS
    MLIRArithDialect
    MLIRFuncDialect
    MLIRSCFDialect
    MLIRAffineDialect
    MLIRTensorDialect
    MLIRLinalgDialect
    MLIRMemRefDialect
    MLIRVectorDialect
    MLIRGPUDialect
    MLIRTransformDialect
    MLIRTransforms
    MLIRTransformUtils
    MLIRLinalgTransformOps
    MLIRPass
    MLIRIR
    MLIRSupport
    MLIRParser
    MLIRBytecodeWriter
    MLIRDialect
    MLIRInferTypeOpInterface
    MLIRCallInterfaces
    MLIRControlFlowInterfaces
    MLIRControlFlowDialect
    MLIRSideEffectInterfaces
    MLIRLinalgTransforms
    MLIRLinalgToStandard
    MLIRLinalgUtils
    MLIRTensorTransforms
    MLIRBufferizationDialect
    MLIRBufferizationTransforms
    MLIRGPUToGPURuntimeTransforms
    MLIRTransformDialect
    MLIRTransformDialectInterfaces
    MLIRLLVMDialect
    MLIRLLVMToLLVMIRTranslation
    MLIRBuiltinToLLVMIRTranslation
    MLIRToLLVMIRTranslationRegistration
    MLIRTargetLLVMIRExport
    MLIRSCFToControlFlow
    MLIRReconcileUnrealizedCasts
    MLIRConvertToLLVMInterface
    MLIRArithToLLVM
    MLIRFuncToLLVM
    MLIRMemRefToLLVM
    MLIRControlFlowToLLVM
    MLIRLinalgToStandard
    MLIRVectorDialect
    MLIRVectorToLLVM
    MLIRVectorToLLVMPass
    MLIRUBDialect
    MLIRUBToLLVM
    MLIRVectorToSCF
    MLIRVectorTransforms
    MLIRLinalgTransforms
)

target_link_libraries(NarvalNirDialect
    PUBLIC
        NarvalProjectIncludes
        ${MLIR_DIALECT_LIBS}
        LLVMOrcJIT
        LLVMJITLink
        LLVMExecutionEngine
        LLVMOrcTargetProcess
        LLVMOrcShared
)

target_compile_options(NarvalNirDialect PRIVATE -fexceptions -fno-rtti -fPIE)

add_executable(NarvalNirTest ${CMAKE_CURRENT_SOURCE_DIR}/nir_pipeline_test.cpp)
add_dependencies(NarvalNirTest NarvalDialectIncGen NarvalTypesIncGen NarvalOpsIncGen NarvalPassesIncGen)
target_include_directories(NarvalNirTest PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${PROJECT_SOURCE_DIR}/include
    ${MLIR_INCLUDE_DIRS}
    ${LLVM_INCLUDE_DIRS}
)

set(NIR_TEST_EXTRA_LIBS
    MLIRConvertToLLVMInterface
    MLIRReconcileUnrealizedCasts
    MLIRSCFToControlFlow
    MLIRControlFlowDialect
    MLIRArithToLLVM
    MLIRFuncToLLVM
    MLIRMemRefToLLVM
    MLIRControlFlowToLLVM
    MLIRLinalgToStandard
    MLIRTransforms
    MLIRLinalgTransforms
)

target_link_libraries(NarvalNirTest PRIVATE
    NarvalNirDialect
    ${NIR_TEST_EXTRA_LIBS}
)
target_compile_options(NarvalNirTest PRIVATE -fexceptions -fno-rtti -fPIE)
