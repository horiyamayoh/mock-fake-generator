#include "ket_ascii.h"
#include "ket_cli.h"
#include "ket_concurrency.h"
#include "ket_contract.h"
#include "ket_file.h"
#include "ket_parse.h"
#include "ket_scope.h"
#include "ket_string.h"

int main()
{
	const auto has_expected_prefix = ket::ascii::StartsWith("mockfakegen", "mock");
	return has_expected_prefix ? 0 : 1;
}
