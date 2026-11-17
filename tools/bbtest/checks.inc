// The checks, one flag each. Included into main.cpp's anonymous namespace.
struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;
};
const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {};
	return list;
}
int runNegative( bool = false ) { return 0; }
int runOffline() { return 0; }
int runBench() { return 0; }
