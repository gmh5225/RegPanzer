#include "MatcherTestData.hpp"
#include "Utils.hpp"
#include "../RegPanzerLib/MatcherGeneratorLLVM.hpp"
#include "../RegPanzerLib/Parser.hpp"
#include "../RegPanzerLib/PushDisableLLVMWarnings.hpp"
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <gtest/gtest.h>
#include "../RegPanzerLib/PopLLVMWarnings.hpp"

namespace RegPanzer
{

namespace
{

std::unique_ptr<llvm::TargetMachine> CreateTargetMachine()
{
	llvm::InitializeAllTargets();
	llvm::InitializeAllTargetMCs();
	llvm::InitializeAllAsmPrinters();
	llvm::InitializeAllAsmParsers();

	const llvm::Triple target_triple(llvm::sys::getDefaultTargetTriple());
	const std::string target_triple_str= target_triple.normalize();

	std::string error_str;
	const auto target= llvm::TargetRegistry::lookupTarget(target_triple_str, error_str);
	if(target == nullptr)
	{
		std::cerr << "Error, selecting target: " << error_str << std::endl;
		return nullptr;
	}

	llvm::SubtargetFeatures features;
	llvm::StringMap<bool> host_features;
	if(llvm::sys::getHostCPUFeatures(host_features))
		for(auto& f : host_features)
			features.AddFeature(f.first(), f.second);

	llvm::TargetOptions target_options;

	const auto code_gen_optimization_level= llvm::CodeGenOpt::Default;

	return
		std::unique_ptr<llvm::TargetMachine>(
			target->createTargetMachine(
				target_triple_str,
				llvm::sys::getHostCPUName(),
				features.getString(),
				target_options,
				llvm::Reloc::Model::PIC_,
				llvm::Optional<llvm::CodeModel::Model>(),
				code_gen_optimization_level));
}

class GeneratedLLVMBinaryMatcherTest : public ::testing::TestWithParam<MatcherTestDataElement> {};

TEST_P(GeneratedLLVMBinaryMatcherTest, TestMatch)
{
	auto target_machine= CreateTargetMachine();
	ASSERT_TRUE(target_machine != nullptr);

	const auto param= GetParam();
	const auto parse_res= RegPanzer::ParseRegexString(param.regex_str);
	const auto regex_chain= std::get_if<RegexElementsChain>(&parse_res);
	ASSERT_TRUE(regex_chain != nullptr);

	const auto regex_graph= BuildRegexGraph(*regex_chain);

	const std::string function_name= "Match";

	llvm::LLVMContext llvm_context;
	auto module= std::make_unique<llvm::Module>("id", llvm_context);
	module->setDataLayout(target_machine->createDataLayout());

	GenerateMatcherFunction(*module, regex_graph, function_name);

	llvm::EngineBuilder builder(std::move(module));
	builder.setEngineKind(llvm::EngineKind::JIT);
	builder.setMemoryManager(std::make_unique<llvm::SectionMemoryManager>());
	const std::unique_ptr<llvm::ExecutionEngine> engine(builder.create(target_machine.release())); // Engine takes ownership over target machine.
	ASSERT_TRUE(engine != nullptr);

	using FunctionType= const char*(*)(const char*, const char*);
	const auto function= reinterpret_cast<FunctionType>(engine->getFunctionAddress(function_name));
	ASSERT_TRUE(function != nullptr);

	for(const MatcherTestDataElement::Case& c : param.cases)
	{
		MatcherTestDataElement::Ranges result_ranges;
		for(size_t i= 0; i < c.input_str.size();)
		{
			const char* const match_end_ptr= function(c.input_str.data() + i, c.input_str.data() + c.input_str.size());

			if(match_end_ptr == nullptr)
				++i;
			else
			{
				const size_t end_offset= size_t(match_end_ptr - c.input_str.data());
				result_ranges.emplace_back(i, end_offset);
				i= end_offset;
			}
		}

		EXPECT_EQ(result_ranges, c.result_ranges);
	}
}

INSTANTIATE_TEST_CASE_P(M, GeneratedLLVMBinaryMatcherTest, testing::ValuesIn(g_matcher_test_data));

} // namespace

} // namespace RegPanzer