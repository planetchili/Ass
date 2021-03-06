#include <iostream>
#include <fstream>
#include <conio.h>
#include "Assembler.h"
#include "Tests.h"

int main( int argc,char* argv[] )
{
	if( argc < 2 )
	{
		std::cout << "No input file specified!" << std::endl;
	}
	else if( std::string{ argv[1] } == "--run-tests" )
	{
		run_tests();
	}
	else
	{
		try
		{
			Assembler a( argv[1] );
			a.Assemble( std::string( argv[1] ) + ".txt" );
		}
		catch( const std::exception& e )
		{
			std::cout << "Fatal error: " << e.what() << std::endl;
		}
	}

	while( !_kbhit() );
	return 0;
}