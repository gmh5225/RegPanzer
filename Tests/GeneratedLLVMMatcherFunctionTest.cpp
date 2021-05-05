#include "MatcherTestData.hpp"
#include "Utils.hpp"
#include "../RegPanzerLib/MatcherGeneratorLLVM.hpp"
#include "../RegPanzerLib/Parser.hpp"
#include "../RegPanzerLib/PushDisableLLVMWarnings.hpp"
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/Interpreter.h>
#include <gtest/gtest.h>
#include "../RegPanzerLib/PopLLVMWarnings.hpp"

namespace RegPanzer
{

namespace
{

const std::string GetTestsDataLayout()
{
	std::string result;

	result+= llvm::sys::IsBigEndianHost ? "E" : "e";
	const bool is_32_bit= sizeof(void*) <= 4u;
	result+= is_32_bit ? "-p:32:32" : "-p:64:64";
	result+= is_32_bit ? "-n8:16:32" : "-n8:16:32:64";
	result+= "-i8:8-i16:16-i32:32-i64:64";
	result+= "-f32:32-f64:64";
	result+= "-S128";

	return result;
}

class GeneratedLLVMMatcherTest : public ::testing::TestWithParam<MatcherTestDataElement> {};

TEST_P(GeneratedLLVMMatcherTest, TestMatch)
{
	const auto param= GetParam();
	const auto regex_chain= RegPanzer::ParseRegexString(param.regex_str);
	ASSERT_NE(regex_chain, std::nullopt);

	const auto regex_graph= BuildRegexGraph(*regex_chain);


	llvm::LLVMContext llvm_context;
	auto module= std::make_unique<llvm::Module>("id", llvm_context);
	module->setDataLayout(GetTestsDataLayout());

	const std::string function_name= "Match";
	GenerateMatcherFunction(*module, regex_graph, function_name);

	llvm::EngineBuilder builder(std::move(module));
	builder.setEngineKind(llvm::EngineKind::Interpreter);
	const std::unique_ptr<llvm::ExecutionEngine> engine(builder.create());
	ASSERT_TRUE(engine != nullptr);

	llvm::Function* const function= engine->FindFunctionNamed(function_name);
	ASSERT_TRUE(function != nullptr);

	for(const MatcherTestDataElement::Case& c : param.cases)
	{
		MatcherTestDataElement::Ranges result_ranges;
		for(size_t i= 0; i < c.input_str.size();)
		{
			llvm::GenericValue args[2];
			args[0].PointerVal= const_cast<char*>(c.input_str.data() + i);
			args[1].PointerVal= const_cast<char*>(c.input_str.data() + c.input_str.size());

			const llvm::GenericValue result_value= engine->runFunction(function, args);
			const char* const match_end_ptr= reinterpret_cast<const char*>(result_value.PointerVal);

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

INSTANTIATE_TEST_CASE_P(M, GeneratedLLVMMatcherTest, testing::ValuesIn(g_matcher_test_data));

} // namespace

} // namespace RegPanzer
