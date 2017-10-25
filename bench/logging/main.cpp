
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <commonpp/core/LoggingInterface.hpp>
#include <boost/program_options.hpp>
#include <boost/timer/timer.hpp>

using namespace boost::program_options;

void benchmark(const variables_map & options)
{
	std::this_thread::sleep_for(std::chrono::seconds(1));
	for (int i=0;i<100;i++)
	{
		GLOG(info) << "Hi biatch!";
	}

}



int main(int argc, char ** argv)
{

	options_description opts;
	opts.add_options()
		("threads", value<int>()->default_value(1))
		("console", value<bool>()->default_value(true))
		("syslog", value<bool>()->default_value(false))
		("help,h", "Show help");   

	variables_map vm;
	store(parse_command_line(argc, argv, opts), vm);

	if (vm.count("help"))
	{
		std::cout << opts;
		exit(1);
	}

	commonpp::core::init_logging();
	if (vm["console"].as<bool>())
	{
		commonpp::core::enable_console_logging();
	}

	if (vm["syslog"].as<bool>())
	{
		commonpp::core::enable_builtin_syslog();
	}

	boost::timer::auto_cpu_timer timer;

	
	std::vector<std::thread> threads;
	for (int i = 0; i<vm["threads"].as<int>(); i++)
	{
		threads.emplace_back([=]() { benchmark(vm); });
	}

	std::for_each(threads.begin(), threads.end(), [](std::thread & t) { t.join(); });



	
}

