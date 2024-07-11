#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include <mutex>
#include <string>
#include <fstream>
#include <cstdint>
#include <iostream>

// Print on console only when in debug mode
#ifdef DEBUG
#define DEBUG_PRINT(x) std::cout << x << std::endl
#define DEBUG_PRINT_INFO(x) std::cout << "\033[34m" << x << "\033[0m" << std::endl    // Text color blue
#define DEBUG_PRINT_WARN(x) std::cout << "\033[33m" << x << "\033[0m" << std::endl    // Text color yellow
#define DEBUG_PRINT_ERROR(x) std::cout << "\033[31m:z" << x << "\033[0m" << std::endl // Text color red
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINT_INFO(x)
#define DEBUG_PRINT_WARN(x)
#define DEBUG_PRINT_ERROR(x)
#endif

using namespace llvm;

namespace
{
    struct BufferMonitor : public ModulePass
    {
        static char ID;

        Module* module;
        std::unique_ptr<IRBuilder<>> builder;

        /*
        Declare mutex for writing to file.
        */
        std::mutex mutex;

        /* 
        Each time a new buffer is found, the bufferID is incremented to assign each buffer a unique id
        The value zero marks an invalid buffer id
        */
        uint32_t bufferID = 1;
        /* This vector contains all ID's of the global data arrays. */
        std::vector<uint32_t> globalsBufferID;
        bool globalDataArraysStoredInMain = false;

        /*
        Each time a new getelementptr instruction is found, the gepID is incremented to assign each getelementptr instruction a unique id
        Don't get confused by the name, this also counts for malloc, calloc, realloc, etc.
        */
        uint64_t gepID = 1;

        // Functions from BufferMonitorLib.c used by the instrumentation
        Function* storeBufferFunction;
        Function* updateBufferFunction;
        Function* storeBufferPointerFunction;

        // Functions used by the instrumentation
        Function* strlenFunction;

        /* 
        Global variable set is set to true if global data arrays are stored so we now, we don't have to store
        them again.
        */
        GlobalVariable* globalDataArraysStored;

        BufferMonitor() : ModulePass(ID)
        {
        }

        Function *GetOrCreateFunction(const std::string &name, Module &module, LLVMContext &context, FunctionType *functionType)
        {
            Function *function = module.getFunction(name);

            if (!function)
                function = Function::Create(functionType, Function::ExternalLinkage, name, &module);

            return function;
        }

        /*
            Reads bufferID and GepID from file.
        */

        void ReadStatsFromFile()
        {
            /* Read bufferID from file */

            // Lock mutex to prevent other threads from writing to the file
            std::lock_guard<std::mutex> lock(mutex);

            std::ifstream file("/var/tmp/ID.log");

            if (!file.is_open())
            {
                std::cout << "Could not read file 'ID.log'. Will create it after module is processed." << std::endl;
                return;
            }

            file >> this->bufferID >> this->gepID;

            file.close();
        }

        /*
            Writes bufferID and GepID from file.
        */

        void WriteStatsToFile()
        {
            /* Write bufferID to file */

            // Lock mutex to prevent other threads from writing to the file
            std::lock_guard<std::mutex> lock(mutex);

            std::ofstream file("/var/tmp/ID.log");
            
            if (!file.is_open())
            {
                std::cerr << "Error: Could not write file 'ID.log'" << std::endl;
                return;
            }

            file << this->bufferID << " " << this->gepID;

            file.close();
        }

        bool init(Module &M)
        {
            std::cout << "Initialize BufferMonitor pass ..." << std::endl;

            this->module = &M;

            // Get context, module and create IRBuilder for instrumentations
            LLVMContext &context = M.getContext();
            builder = std::make_unique<IRBuilder<>>(context);

            /*
                Get all functions used by the instrumentation from BufferMonitorLib.c
            */

            FunctionType* storeBufferFunctionType = FunctionType::get(Type::getVoidTy(context), {Type::getInt32Ty(context), Type::getInt8PtrTy(context), Type::getInt64Ty(context), Type::getInt64Ty(context)}, false);
            this->storeBufferFunction = GetOrCreateFunction("store_buffer", *module, context, storeBufferFunctionType);

            FunctionType* updateBufferFunctionType = FunctionType::get(Type::getVoidTy(context), {Type::getInt64Ty(context), Type::getInt8PtrTy(context), Type::getInt64Ty(context)}, false);
            this->updateBufferFunction = GetOrCreateFunction("update_buffer", *module, context, updateBufferFunctionType);

            FunctionType* storeBufferPointerFunctionType = FunctionType::get(Type::getVoidTy(context), {Type::getInt32Ty(context), Type::getInt8PtrTy(context), Type::getInt8PtrTy(context), Type::getInt64Ty(context)}, false);
            this->storeBufferPointerFunction = GetOrCreateFunction("store_buffer_pointer", *module, context, storeBufferPointerFunctionType);

            FunctionType* strlenFunctionType = FunctionType::get(Type::getInt64Ty(context), {Type::getInt8PtrTy(context)}, false);
            this->strlenFunction = GetOrCreateFunction("strlen", *module, context, strlenFunctionType);

            return true;
        }

        /*
        GEP instructions can be nested in load instructions. This function extracts the gep instruction from 
        the load instruction. This function returns the GEP instruction if present, else nullptr is returned.
        */

        GetElementPtrInst* ExtractGEPFromLoadInstruction(LoadInst* loadInst)
        {
            Value* loadInstructionOperand = loadInst->getPointerOperand();
            GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(loadInstructionOperand);

            if (gepInst)
            {
                return gepInst;
            }

            return nullptr;
        }

        /*
        GEP instructions can be nested in store instructions. This function extracts the gep instruction from 
        the store instruction. This function returns the GEP instruction if present, else nullptr is returned.
        */

        GetElementPtrInst* ExtractGEPFromStoreInstruction(StoreInst* storeInst) 
        {
            Value* loadInstructionOperand = storeInst->getPointerOperand();
            GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(loadInstructionOperand);

            if (gepInst)
            {
                return gepInst;
            }

            return nullptr;
        }

        virtual bool runOnModule(Module& M)
        {
            DEBUG_PRINT_INFO("Run pass in debug mode");

            init(M);

            LLVMContext &context = M.getContext();

            /* Read IDs from the ID.log file */

            ReadStatsFromFile();

            assignGlobalConstantsIDs(M, builder);

            /* Get the boolean type (i1 in LLVM) */
            Type* BoolType = llvm::Type::getInt1Ty(context);

            /* Create a global boolean variable initialized to 'false', with internal linkage */
            Constant* InitialValue = ConstantInt::get(BoolType, 0); 
            this->globalDataArraysStored = new GlobalVariable(
                M,
                BoolType,
                false,
                /* Use InternalLinkage here because this has to be done in every module. */
                llvm::GlobalVariable::InternalLinkage,  
                InitialValue,
                "globalDataArraysStored"
            );

            /* Try to insert global data arrasy in main funtion. */
            getGlobalConstants(M, builder);

            Function* storeGlobalDataArraysFunction = createStoreGlobalDataArraysFunction(M, context, builder);

            // Iterate over all functions in the module
            for (Function& F : M)
            {
                if (F.isDeclaration() || F.empty() || F.getEntryBlock().empty())
                {
                    continue;
                }

                /* Get function name */
                StringRef functionName = F.getName();

                if (functionName == "storeGlobalDataArraysFunction")
                {
                    continue;
                }
    
                /* Ignore all ASAN functions. */
                if (functionName.contains("asan"))
                {
                    continue;
                }

                /* In the case there is no main function, we try to store it in this function. */
                if (!this->globalDataArraysStoredInMain && this->globalsBufferID.size() != 0) 
                {
                    IRBuilder<> localBuilder(&F.getEntryBlock(), F.getEntryBlock().begin());
                    localBuilder.CreateCall(storeGlobalDataArraysFunction, {}); // Assuming the function takes no arguments
                }

                procesFunction(F);
            }

            /* 
            After the module was processed we write the ID's to a file so and can be
            used when the next module is processed. 
            */

            WriteStatsToFile();

            return true;
        }

        /*
            Responsible for handling global data arrays. If the 'function' parameter is nullptr,
            the function will try to add the global data arrays in the main function. Otherwise
            the function will try to add the global data arrays in the function that is passed as
            parameter.
        */

        void getGlobalConstants(Module& M, std::unique_ptr<IRBuilder<>>& builder, BasicBlock* basicBlock = nullptr) 
        {
            
            if (this->globalDataArraysStoredInMain == true)
            {
                /* We already have stored all global data arrays of this module in the main function, so we can leave again */
                return;
            }
            
            LLVMContext& context = M.getContext();

            Constant* zeroValue = ConstantInt::get(Type::getInt64Ty(context), 0);

            /* Get main function */
            Function* mainFunction = M.getFunction("main");

            if (mainFunction)
            {
                /* Since we have a main function, we can store all global data arrays in it with this call. */
                this->globalDataArraysStoredInMain = true;
            
                if (mainFunction->empty() || mainFunction->getEntryBlock().empty())
                {
                    return;
                }
            }

            /*
            If there is no main function, try to add the global data arrays in the function that is passed as parameter.
            */
            if (!mainFunction && basicBlock == nullptr)
            {
                /* We have no entry point given so we just return. */
                return;
            }


            /* If main function exists in this module we insert global data arrays there, else set insert point to the passed basic block. */
            if (mainFunction)
            {
                builder->SetInsertPoint(&mainFunction->getEntryBlock(), mainFunction->getEntryBlock().begin());

            } else 
            {
                builder->SetInsertPoint(basicBlock, basicBlock->begin());
            }

            int index = 0;
            for (auto &global : M.globals()) {
                if (global.getType()->isPointerTy()) { 
                    Type *elementType = global.getType()->getPointerElementType();
                    if (elementType->isArrayTy()) {  
                        std::cout << "Found global array: " << global.getName().str() << std::endl;

                        /* Skip if @llvm.global_ctors */
                        if (!global.getName().contains("global_ctors"))
                        {
                            std::cout << "Store global array: " << global.getName().str() << std::endl;

                            ArrayType *arrayType = dyn_cast<ArrayType>(elementType);
                            if (arrayType) 
                            {
                                uint32_t globalBufferID = this->globalsBufferID[index];
                                
                                Value* bufferIDValue = ConstantInt::get(Type::getInt32Ty(context), globalBufferID);

                                // Determine size of global data array
                                uint64_t arraySizeInBytes = arrayType->getNumElements() * (arrayType->getElementType()->getPrimitiveSizeInBits() / 8);
                                Value* arraySizeInBytesValue = ConstantInt::get(Type::getInt64Ty(context), arraySizeInBytes);

                                // Get constant array address
                                // Value* bufferAddress = builder->CreateBitCast(&global, Type::getInt8PtrTy(context));

                                this->builder->CreateCall(storeBufferFunction, {bufferIDValue, &global, arraySizeInBytesValue, zeroValue});
                            }
                            index++;

                        }

                    }
                }
            }
        }

        void assignGlobalConstantsIDs(Module& M, std::unique_ptr<IRBuilder<>>& builder) {
            if (this->globalsBufferID.size() != 0)
            {
                return;
            }

            for (GlobalVariable& global : M.globals()) {
                if (global.hasInitializer()) {
                    this->globalsBufferID.push_back(this->bufferID);
                    std::cout << "Found global! Assign ID: " << this->bufferID << std::endl;
                    this->bufferID++;
                }
            }
        }

        Function* createStoreGlobalDataArraysFunction(Module& M, LLVMContext& context, std::unique_ptr<IRBuilder<>>& builder) 
        {
            /* Create new function. */
            FunctionType* funcType = FunctionType::get(Type::getVoidTy(context), false);
            Function* newFunction = Function::Create(funcType, Function::InternalLinkage, "storeGlobalDataArraysFunction", &M);

            /* Create basic blocks for the new function */
            BasicBlock* entryBlock = BasicBlock::Create(context, "entry", newFunction);
            BasicBlock* trueBlock = BasicBlock::Create(context, "trueBlock", newFunction);
            BasicBlock* exitBlock = BasicBlock::Create(context, "exitBlock", newFunction);

            builder->SetInsertPoint(entryBlock);

            /* Get global var that holds information if we already have stored global data arrays. */
            GlobalVariable* globalDataArraysStored = M.getNamedGlobal("globalDataArraysStored");

            Type* BoolType = llvm::Type::getInt1Ty(context);

            /* Load global variable and decide branching. */
            LoadInst* globalDataArraysStoredValue = builder->CreateLoad(globalDataArraysStored);
            globalDataArraysStoredValue->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(context, None));

            builder->CreateCondBr(globalDataArraysStoredValue, exitBlock, trueBlock); 

            builder->SetInsertPoint(trueBlock);
            getGlobalConstants(M, builder, trueBlock);

            /* Store true back to globalDataArraysStored after execution of trueBlock */
            StoreInst* storeInstruction = builder->CreateStore(ConstantInt::get(BoolType, 1), globalDataArraysStored);
            storeInstruction->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(context, None));

            //* Exit function. */
            builder->CreateBr(exitBlock);

            builder->SetInsertPoint(exitBlock);
            builder->CreateRetVoid();

            return newFunction;
        }


        /* 
        Create a basic block for stroring all global data arrays from this module and add it to the beginning of the 
        passed function.
        */

        void storeGlobalDataArraysBB(Module& M, Function* function, std::unique_ptr<IRBuilder<>>& builder) {
            LLVMContext& context = M.getContext();

            if (!function || function->empty() || function->getEntryBlock().empty())
            {
                return;
            }

            Type* BoolType = llvm::Type::getInt1Ty(context);

            /* Create basic blocks */
            BasicBlock* entryBlock = BasicBlock::Create(context, "entry", function);
            BasicBlock* trueBlock = BasicBlock::Create(context, "trueBlock", function);

            /* Get the original entry block */
            BasicBlock* originalEntryBlock = &function->getEntryBlock();

            /* Relink the new entry block to be the first block */
            entryBlock->moveBefore(originalEntryBlock);
            
            /* Set insert point to entry block */
            builder->SetInsertPoint(entryBlock);

            /* Load global variable that decides if we jump to trueBlock or falseBlock */
            LoadInst* globalDataArraysStoredValue = builder->CreateLoad(globalDataArraysStored);

            /* To make Address Sanitizer ignore this store instruction. */
            globalDataArraysStoredValue->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(context, None));

            /* Create a conditional branch */
            builder->CreateCondBr(globalDataArraysStoredValue, originalEntryBlock, trueBlock);

            /* Set insert point to trueBlock */
            builder->SetInsertPoint(trueBlock);

            /* This function creates the store operations and inserts them in the passed basic block */
            getGlobalConstants(M, builder, trueBlock);

            /* After executing the trueBlock, we set the 'globalDataArraysStored' to true, so we don't store them again. */
            StoreInst* storeInstruction = builder->CreateStore(ConstantInt::get(BoolType, 1), globalDataArraysStored);

            /* To make Address Sanitizer ignore this store instruction. */
            storeInstruction->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(context, None));

            /* Create an unconditional branch to the falseBlock */
            builder->CreateBr(originalEntryBlock);

            return;
        }

        /*
            Responsible for handling functions for dynamic memory allocation: malloc, calloc, realloc, new
        */

        void processDynamicAllocation(StringRef functionName, CallInst* callInst, LLVMContext& context)
        {
            // Check if called function is malloc or new
            if (functionName == "malloc" || functionName == "calloc" || functionName.startswith("_Znwm") || functionName.startswith("_Znam"))
            {
                Value* bufferAddress = callInst;                     // get address of allocated buffer

                Value* bufferSizeValue;

                if (functionName == "calloc")
                {
                    Value* numElementsValue = callInst->getArgOperand(0);
                    Value* elementSizeValue = callInst->getArgOperand(1);

                    // Calculate size in bytes
                    bufferSizeValue = builder->CreateMul(numElementsValue, elementSizeValue);
                } else 
                {
                    bufferSizeValue = callInst->getArgOperand(0); // get size of allocated buffer
                }

                Constant* bufferIDValue = ConstantInt::get(Type::getInt32Ty(context), this->bufferID);
                Constant* zeroValue = ConstantInt::get(Type::getInt64Ty(context), 0);

                // Store dynamically allocated buffer in linked list
                this->builder->CreateCall(storeBufferFunction, {bufferIDValue, bufferAddress, bufferSizeValue, zeroValue});

                // Increment bufferID
                std::cout << "Stored buffer with ID: " << this->bufferID << std::endl;
                this->bufferID++;
            } else if (functionName == "realloc")
            {
                Value* bufferAddress = callInst;                     // get address of allocated buffer

                Value* bufferSizeValue = callInst->getArgOperand(1); // get size of allocated buffer

                Constant* bufferIDValue = ConstantInt::get(Type::getInt32Ty(context), this->bufferID);
                Constant* isReallocFunctionCall = ConstantInt::get(Type::getInt64Ty(context), 1);

                // Store dynamically allocated buffer in linked list
                std::cout << "Stored buffer with ID: " << this->bufferID << std::endl;
                this->builder->CreateCall(storeBufferFunction, {bufferIDValue, bufferAddress, bufferSizeValue, isReallocFunctionCall});
            }
        }

        /*
            Responsible for handling standard C functions: memcpy, strcpy, strcat, ...
        */

        void processStandardCFunctions(StringRef functionName, CallInst* callInst, LLVMContext& context)
        {
            if (functionName.contains("memcpy"))
            {
                Value* destBufferAddress = callInst->getArgOperand(0); // get address of destination buffer
                Value* srcBufferAddress = callInst->getArgOperand(1);  // get address of source buffer
                Value* bufferSizeValue = callInst->getArgOperand(2);   // get amount of bytes to copy

                // Decrement size by one, since we want the last byte that was manipulated.
                Value* lastManipulatedByte = builder->CreateSub(bufferSizeValue, ConstantInt::get(Type::getInt64Ty(context), 1));

                // Create gepID value
                Value* gepIDValue1 = ConstantInt::get(Type::getInt64Ty(context), this->gepID);

                // Update buffer data 
                this->builder->CreateCall(updateBufferFunction, {gepIDValue1, destBufferAddress, lastManipulatedByte});
                
                // Increment gepID
                this->gepID++;

                // Create gepID value
                Value* gepIDValue2 = ConstantInt::get(Type::getInt64Ty(context), this->gepID);
                
                this->builder->CreateCall(updateBufferFunction, {gepIDValue2, srcBufferAddress, lastManipulatedByte});

                // Increment gepID
                this->gepID++;

            } else if (functionName.contains("memset") )
            {
                Value* destBufferAddress = callInst->getArgOperand(0); // get address of destination buffer
                Value* bufferSizeValue = callInst->getArgOperand(2);   // get amount of bytes to copy

                // Decrement size by one, since we want the last byte that was manipulated.
                Value* lastManipulatedByte = builder->CreateSub(bufferSizeValue, ConstantInt::get(Type::getInt64Ty(context), 1));

                // Create gepID value
                Value* gepIDValue = ConstantInt::get(Type::getInt64Ty(context), this->gepID);

                // Update buffer data
                this->builder->CreateCall(updateBufferFunction, {gepIDValue, destBufferAddress, lastManipulatedByte});

                // Increment gepID
                this->gepID++;

            } else if (functionName.contains("strcpy"))
            {
                Value* destBufferAddress = callInst->getArgOperand(0); // get address of destination buffer
                Value* sourceBufferAddress = callInst->getArgOperand(1); // get address of source buffer

                // Get size of source buffer and check if destination buffer is large enough to hold the string.
                Value* sourceBufferSize = this->builder->CreateCall(strlenFunction, {sourceBufferAddress});

                // Decrement size by one, since we want the last byte that was manipulated.
                sourceBufferSize = builder->CreateSub(sourceBufferSize, ConstantInt::get(Type::getInt64Ty(context), 1));

                // Create gepID value
                Value* gepIDValue = ConstantInt::get(Type::getInt64Ty(context), this->gepID);

                // Update buffer data
                this->builder->CreateCall(updateBufferFunction, {gepIDValue, destBufferAddress, sourceBufferSize});

                // Increment gepID
                this->gepID++;
                
            }
        }

        bool procesFunction(Function& F)
        {
            DEBUG_PRINT_INFO("Pass on function: " << F.getName().str());

            LLVMContext &context = F.getContext();

            auto I = inst_begin(F);
            auto nextInstruction = I;
            while (I != inst_end(F))
            {
                nextInstruction++;

                // Skip instruction if the 'gepinstruction' metadata is set
                if (I->getMetadata("gepinstruction"))
                {
                    I = nextInstruction;
                    continue;
                }

                this->builder->SetInsertPoint(&*I);

                // Check if the current instruction is an alloca instruction
                if (AllocaInst *allocaInst = dyn_cast<AllocaInst>(&*I))
                {
                    /*
                        This is an static allocation
                    */

                    // For Variable Length Arrays (VLA) the array size is not a constant
                    if (!isa<ConstantInt>(allocaInst->getArraySize())) 
                    {
                        /*
                            This might be a VLA
                        */

                        // Set insert point behind the current instruction
                        builder->SetInsertPoint(I->getNextNode());

                        Value* arraySizeValue = allocaInst->getArraySize();

                        // Calculate size in bytes
                        unsigned elementSizeInBytes = allocaInst->getAllocatedType()->getPrimitiveSizeInBits() / 8;

                        Value* arraySizeInBytesValue = builder->CreateMul(arraySizeValue, ConstantInt::get(Type::getInt64Ty(context), elementSizeInBytes));

                        // Cast the pointer to the buffer to a generic i8* pointer
                        Value *bufferAddress = allocaInst;
                        if (allocaInst->getType() != Type::getInt8PtrTy(context))
                        {
                            bufferAddress = builder->CreateBitCast(allocaInst, Type::getInt8PtrTy(context));
                        }

                        Constant* bufferIDValue = ConstantInt::get(Type::getInt32Ty(context), this->bufferID);
                        Constant* zeroValue = ConstantInt::get(Type::getInt64Ty(context), 0);

                        // Store statically allocated buffer in linked list
                        this->builder->CreateCall(storeBufferFunction, {bufferIDValue, bufferAddress, arraySizeInBytesValue, zeroValue});

                        // Increment bufferID
                        std::cout << "Stored buffer with ID: " << this->bufferID << std::endl;
                        this->bufferID++;
                    }
                    else if (ArrayType *arrayType = dyn_cast<ArrayType>(allocaInst->getAllocatedType()))
                    {
                        /*
                            This is a static array
                        */
                        
                        // Set insert point after current instruction
                        this->builder->SetInsertPoint(I->getNextNode());

                        // Get size of the array in bytes
                        unsigned arraySize = arrayType->getNumElements();
                        unsigned elementSizeInBytes = arrayType->getElementType()->getPrimitiveSizeInBits() / 8;
                        unsigned arraySizeInBytes = arraySize * elementSizeInBytes;

                        // Convert arraySize to LLVM Value* for inserting into the linked list
                        Value *bufferSizeValue = ConstantInt::get(Type::getInt64Ty(context), arraySizeInBytes);

                        // Cast the pointer to the buffer to a generic i8* pointer
                        Value *bufferAddress = allocaInst;
                        if (allocaInst->getType() != Type::getInt8PtrTy(context))
                        {
                            bufferAddress = builder->CreateBitCast(allocaInst, Type::getInt8PtrTy(context));
                        }

                        Constant* bufferIDValue = ConstantInt::get(Type::getInt32Ty(context), this->bufferID);
                        Constant* zeroValue = ConstantInt::get(Type::getInt64Ty(context), 0);

                        // Store statically allocated buffer in linked list
                        this->builder->CreateCall(storeBufferFunction, {bufferIDValue, bufferAddress, bufferSizeValue, zeroValue});

                        // Increment bufferID
                        std::cout << "Stored buffer with ID: " << this->bufferID << std::endl;
                        this->bufferID++;
                    }
                } else if (auto callInst = dyn_cast<CallInst>(&*I))
                {
                    /*
                        This is an call instruction
                    */

                    Function *calledFunc = callInst->getCalledFunction();
                    if (calledFunc)
                    {
                        // Get name of called function
                        StringRef funcName = calledFunc->getName();
                        
                        // Set insert point behind the current instruction
                        builder->SetInsertPoint(I->getNextNode());

                        processDynamicAllocation(funcName, callInst, context);

                        processStandardCFunctions(funcName, callInst, context);
                    }
                } 
                

                GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(&*I);
                if (!gepInst)
                {
                    /* 
                    If current instruction is not a gep instruction, check if it is a load or store instruction. 
                    Load and store instructions can have gep instructions nested in them.
                    */
                    /*
                    if (LoadInst* loadInst = dyn_cast<LoadInst>(&*I))
                    {
                        gepInst = ExtractGEPFromLoadInstruction(loadInst);
                    } else if (StoreInst* storeInst = dyn_cast<StoreInst>(&*I))
                    {
                        gepInst = ExtractGEPFromStoreInstruction(storeInst);
                    }
                    */
                }

                
                if (gepInst)
                {
                    /*
                        This is a getelementptr instruction, a buffer is being accessed here
                    */

                    if (gepInst->hasAllZeroIndices())
                    {
                        I = nextInstruction;
                        continue;
                    }

                    /* If gep instruction is performed on a strcut object skip. */
                    if (gepInst->getPointerOperandType()->isPointerTy())
                    {
                        if (gepInst->getPointerOperandType()->getPointerElementType()->isStructTy())
                        {
                            I = nextInstruction;
                            continue;
                        }
                    }

                    builder->SetInsertPoint(&*I);

                    // Get base pointer of the buffer
                    Value *basePtr = gepInst->getPointerOperand();

                    // Get type of the base pointer to determine the size of the elements
                    Type *baseType = basePtr->getType();
                    unsigned elementSizeInBytes = 0;
                    if (PointerType *ptrType = dyn_cast<PointerType>(baseType))
                    {
                        // Get type of the base pointer
                        baseType = ptrType->getPointerElementType();

                        if (ArrayType *arrayType = dyn_cast<ArrayType>(baseType))
                        {
                            // It's an array. Get its element type and then its size.
                            Type *elementType = arrayType->getElementType();
                            elementSizeInBytes = elementType->getPrimitiveSizeInBits() / 8;
                        }
                        else
                        {
                            // It's a pointer. Get the size of the type it points to.
                            elementSizeInBytes = baseType->getPrimitiveSizeInBits() / 8;
                        }
                    }

                    int iteration = 0;
                    for (auto it = gepInst->idx_begin(); it != gepInst->idx_end(); it++, iteration++)
                    {
                        /* Determine accessed index */
                        Value *indexValue = it->get();

                        /* Skip if indexValue is a constant zero */
                        if (ConstantInt *constIndexValue = dyn_cast<ConstantInt>(indexValue))
                        {
                            if (constIndexValue->isZero())
                            {
                                continue;
                            }
                        }

                        if (elementSizeInBytes == 0)
                        {
                            /* 
                            It seems, that sometimes the size of the underlying data type cannot be determined and in this case
                            we just use the index.
                            */

                            elementSizeInBytes = 1;
                        }

                        /* Multiplay the accessed index by the size of the element to get accessed byte instead of index. */
                        Value *accessedByte = builder->CreateMul(indexValue, ConstantInt::get(Type::getInt64Ty(context), elementSizeInBytes));

                        /* 
                        To get the last read/written byte instead of the first read/written byte, add size of base type to accessed index. 
                        This addition behaves weird sometimes.
                        */
                        // unsigned offset = elementSizeInBytes - 1;
                        // accessedByte = builder->CreateAdd(accessedByte, ConstantInt::get(Type::getInt64Ty(context), offset));                        

                        /*
                            Add the elementSizeInBytes value to accessedByte, since the last accessed byte is the accessed index plus the 
                            size of the element
                        */

                        // Cast the pointer to the buffer to a generic i8* pointer
                        if (basePtr->getType() != Type::getInt8PtrTy(context))
                        {
                            basePtr = builder->CreateBitCast(basePtr, Type::getInt8PtrTy(context));
                        }

                        // Create gepID value
                        Value* gepIDValue = ConstantInt::get(Type::getInt64Ty(context), this->gepID);

                        // Update the highest accessed byte of the currentl acessed buffer if accessedByte > highest_accessed_byte
                        this->builder->CreateCall(updateBufferFunction, {gepIDValue, basePtr, accessedByte});

                        // Increment gepID
                        this->gepID++;

#ifdef TRACK_BUFFER_POINTERS
                        /*
                        To track every buffer access, we also have to store pointers that point to the buffer.
                        This is done by storing the returned pointer of the getelementptr instruction.
                        */

                        /* Move insert point after the current gep instruction. */
                        builder->SetInsertPoint(gepInst->getNextNode());
                    
                        /* Get the address returned by gep instruction */
                        Value *gepAddress = gepInst;

                        Constant* bufferIDValue = ConstantInt::get(Type::getInt32Ty(context), this->bufferID);


                        /* Store the buffer pointer */
                        this->builder->CreateCall(storeBufferPointerFunction, {bufferIDValue, basePtr, gepAddress, accessedByte});

                        // Increment bufferID
                        std::cout << "Stored buffer with ID: " << this->bufferID << std::endl;
                        this->bufferID++;
#endif
                    }
                }

                I = nextInstruction;
            }

            return true;
        }
    };

} // namespace

char BufferMonitor::ID = 2;

static void registerBufferMonitorPass(const PassManagerBuilder &,
                                      legacy::PassManagerBase &PM)
{
    PM.add(new BufferMonitor());
}

// Run BufferMonitor at the end of the optimization pipeline
static RegisterStandardPasses RegisterBufferMonitorPass(
    PassManagerBuilder::EP_ModuleOptimizerEarly, registerBufferMonitorPass);

// Also run BufferMonitor when optimizations are turned off (-O0)
static RegisterStandardPasses RegisterBufferMonitorPass0(
    PassManagerBuilder::EP_EnabledOnOptLevel0, registerBufferMonitorPass);