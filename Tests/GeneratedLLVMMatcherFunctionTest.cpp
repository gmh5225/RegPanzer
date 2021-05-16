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
	const auto parse_res= RegPanzer::ParseRegexString(param.regex_str);
	const auto regex_chain= std::get_if<RegexElementsChain>(&parse_res);
	ASSERT_TRUE(regex_chain != nullptr);

	const auto regex_graph= BuildRegexGraph(*regex_chain, Options());

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
			size_t group[2]{0, 0};

			llvm::GenericValue args[5];
			args[0].PointerVal= const_cast<char*>(c.input_str.data());
			args[1].IntVal= llvm::APInt(sizeof(size_t) * 8, c.input_str.size());
			args[2].IntVal= llvm::APInt(sizeof(size_t) * 8, i);
			args[3].PointerVal= &group;
			args[4].IntVal= llvm::APInt(sizeof(size_t) * 8, 1);

			const llvm::GenericValue result_value= engine->runFunction(function, args);
			const auto subpatterns_extracted= result_value.IntVal.getLimitedValue();

			if(subpatterns_extracted == 0)
				++i;
			else
			{
				result_ranges.emplace_back(group[0], group[1]);
				if(group[1] <= i && group[1] <= group[0])
					break;
				i= group[1];
			}
		}

		EXPECT_EQ(result_ranges, c.result_ranges);
	}
}

INSTANTIATE_TEST_CASE_P(M, GeneratedLLVMMatcherTest, testing::ValuesIn(g_matcher_test_data, g_matcher_test_data + g_matcher_test_data_size));

} // namespace

} // namespace RegPanzer
