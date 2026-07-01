#include "ket_cli.h"
#include "ket_parse.h"

int main(int argc, char** argv)
{
	const ket::cli::ArgvView args(argc, argv);
	const auto parsed = ket::parse::UInt<unsigned>("23");
	return args.Size() > 0U && parsed.has_value() && *parsed == 23U ? 0 : 1;
}
