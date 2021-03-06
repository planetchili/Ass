#pragma once

#include <iostream>
#include <fstream>
#include <map>
#include <variant>
#include <optional>
#include <sstream>
#include <exception>
#include <locale>
#include <vector>
#include <memory>
#include <iomanip>
#include <assert.h>
#include "Trim.h"
#include "Parsing.h"
#include "Symbol.h"
#include "Instruction.h"
#include "Directive.h"

class Assembler
{
public:
	Assembler( std::string name )
	{
		RegisterOperations();

		std::ifstream file;
		file.exceptions( std::ifstream::failbit | std::ifstream::badbit );
		file.open( name );
		ProcessSourceStream( file );
	}
	Assembler( std::istream& file )
	{
		RegisterOperations();

		ProcessSourceStream( file );
	}
	void Emit( unsigned char byte )
	{
		ValidateAddress();
		ram[address++] = byte;
	}
	void SetAddress( int address )
	{
		this->address = address;
		ValidateAddress();
	}
	int GetAddress() const
	{
		ValidateAddress();
		return address;
	}
	void AddLabel( const std::string& name,int address,int line )
	{
		symbols.emplace( name,Symbol::MakeLabel( name,line,address ) );
	}
	void AddVariable( const std::string& name,int address,int line )
	{
		symbols.emplace( name,Symbol::MakeVariable( name,line,address ) );
	}
	void AddLabelReference( const std::string& name,int address,int line )
	{
		symbolReferences.emplace( name,Symbol::MakeLabel( name,line,address ) );
	}
	void AddVariableReference( const std::string& name,int address,int line )
	{
		symbolReferences.emplace( name,Symbol::MakeVariable( name,line,address ) );
	}
	void Assemble( std::string name )
	{
		bool error = false;
		for( auto lp : sourceLines )
		{
			try
			{
				ProcessLine( lp.first,lp.second );
			}
			catch( const std::exception& e )
			{
				error = true;
				mout << e.what() << std::endl;
			}
		}

		if( error )
		{
			throw std::exception( "Assembly halted before dependency resolution due to errors!" );
		}

		mout << "Source code successfully processed." << std::endl;

		ResolveDependencies();

		mout << "Symbol dependencies successfuly resolved." << std::endl;

		std::ofstream file;
		file.exceptions( std::ifstream::failbit | std::ifstream::badbit );
		file.open( name );
		file << "v2.0 raw\n";
		int byteCount = 0;
		int usedBytes = 0;
		for( auto b : ram )
		{
			file << std::hex << std::setfill( '0' ) << std::setw( 2 )
				<< int( b.value_or( 0 ) ) << " ";

			if( b.has_value() )
			{
				usedBytes++;
			}

			if( ++byteCount >= 8 )
			{
				byteCount = 0;
				file << "\n";
			}
		}

		mout << "Binary image successfully generated." << std::endl;
		mout << "Bytes used: [ " << usedBytes << " ]  Bytes free: [ " 
			<< 256 - usedBytes << " ]" << std::endl;
	}

private:
	void ProcessSourceStream( std::istream& file )
	{
		// should maybe rename function if we're doing this?
		ram.resize( ramsize );

		file.exceptions( std::ifstream::failbit | std::ifstream::badbit );
		int lineNum = 1;
		std::string line;
		try
		{
			while( std::getline( file,line ) )
			{
				sourceLines.emplace_back( line,lineNum++ );
			}
		}
		catch( const std::ifstream::failure& e )
		{
			if( !file.eof() )
			{
				throw e;
			}
		}
	}
	void ProcessLine( std::string& line,int lineNum )
	{
		strip_comment( line );

		{
			auto t = extract_token_white( line );

			if( !t.has_value() )
			{
				return;
			}

			if( is_label( t.value() ) )
			{
				// remove the colon
				t.value().pop_back();
				if( symbols.count( t.value() ) > 0 )
				{
					std::stringstream msg;
					msg << "Cannot create label at line <" << lineNum << ">! Symbol [" << t.value() << "] already exists!";
					throw std::exception( msg.str().c_str() );
				}

				if( extract_token_white( line ).has_value() )
				{
					std::stringstream msg;
					msg << "Cannot create label at line <" << lineNum << ">! There is other garbage on that line!";
					throw std::exception( msg.str().c_str() );
				}
				// add label
				AddLabel( t.value(),GetAddress(),lineNum );
			}
			else if( is_name( t.value() ) ) // could be instruction OR labeled directive
			{
				// we can't start with an existing symbol
				if( symbols.count( t.value() ) > 0 )
				{
					std::stringstream msg;
					msg << "Found a symbol at line <" << lineNum << ">! Symbol [" << t.value() << "] already exists and I hate you!";
					throw std::exception( msg.str().c_str() );
				}
				else if( instructions.count( t.value() ) > 0 )
				{
					// process instruction (trashes line)
					instructions[t.value()]->Process( *this,t.value(),line,lineNum );
				}
				else // not instruction; could be a directive...!
				{
					auto d = extract_token_white( line );
					if( !d.has_value() )
					{
						std::stringstream msg;
						msg << "Found a symbol at line <" << lineNum << ">! Symbol [" << t.value() << "]. Expected directive afterwards. Found nothing. Is sad now.";
						throw std::exception( msg.str().c_str() );
					}
					else if( !is_directive( d.value() ) )
					{
						std::stringstream msg;
						msg << "Found a symbol at line <" << lineNum << ">! Symbol [" << t.value() << "]. Expected directive afterwards. Found ["
							<< d.value() << "]. Wtf bro?";
						throw std::exception( msg.str().c_str() );
					}
					else
					{
						// strip off directive dot
						d.value().erase( d.value().begin(),d.value().begin() + 1 );
						if( directives.count( d.value() ) == 0 )
						{
							std::stringstream msg;
							msg << "Unknown directive at line <" << lineNum << ">! After new symbol [" << t.value() << "], found [."
								<< d.value() << "]. Do you even code bro?";
							throw std::exception( msg.str().c_str() );
						}
						// process labeled directive
						directives[d.value()]->Process( *this,d.value(),t.value(),std::move( line ),lineNum );
					}
				}
			}
			else if( is_directive( t.value() ) )
			{
				// strip off directive dot
				t.value().erase( t.value().begin(),t.value().begin() + 1 );
				if( directives.count( t.value() ) == 0 )
				{
					std::stringstream msg;
					msg << "Unknown directive at line <" << lineNum << ">! Found [."
						<< t.value() << "]. tsk tsk.";
					throw std::exception( msg.str().c_str() );
				}
				// process directive
				directives[t.value()]->Process( *this,t.value(),std::move( line ),lineNum );
			}
			else
			{
				std::stringstream msg;
				msg << "What is this? I can't even. at line <" << lineNum << ">! It sez [" << t.value() << "].";
				throw std::exception( msg.str().c_str() );
			}
		}
	}
	void ResolveDependencies()
	{
		// loop through all symbols and resolve their dependencies
		for( auto it = symbols.cbegin(),next_it = symbols.cbegin(); 
			it != symbols.cend(); it = next_it )
		{
			next_it = std::next( it );

			// remove all matching references after writing in address in ram
			bool unreferenced = true;
			auto irp = symbolReferences.equal_range( it->first );
			while( irp.first != irp.second )
			{
				if( irp.first->second.GetType() == it->second.GetType() )
				{
					// fill ram with referenced address
					ram[irp.first->second.GetAddress()] = it->second.GetAddress();
					// erase reference
					unreferenced = false;
					symbolReferences.erase( irp.first++ );
				}
				else
				{
					mout << "Reference <" << irp.first->first << "> from line ["
						<< irp.first->second.GetLine() << "] does not match symbol <"
						<< it->first << "> defined at line [" << it->second.GetLine()
						<< "]" << std::endl;
					irp.first++;
				}
			}
			// erase symbol if it was referenced at least once
			if( !unreferenced )
			{
				symbols.erase( it );
			}
		} // end of symbol resolution loop

		// show warnings for unrefd symbols
		for( auto& sp : symbols )
		{
			mout << "(warning) Unreferenced symbol <" << sp.first << "> declared "
				<< "at line [" << sp.second.GetLine() << "]" << std::endl;
		}

		// show errors for unresolved references
		for( auto& rp : symbolReferences )
		{
			mout << "Unresolved reference <" << rp.first << "> referenced "
				<< "at line [" << rp.second.GetLine() << "] !!" << std::endl;
		}

		if( symbolReferences.size() > 0 )
		{
			throw std::exception( "Halting assembly due to unresolved references!!" );
		}		
	}
	void ValidateAddress() const
	{
		if( address < 0 || address >= int( ram.size() ) )
		{
			std::stringstream msg;
			msg << "Assembling address out of bounds: " << std::hex << "0x" << address;
			msg << " (" << std::dec << address << ")";
			throw std::exception( msg.str().c_str() );
		}
	}
	void RegisterOperations();
	template<class T>
	void RegisterInstruction( std::string name )
	{
		assert( instructions.count( name ) == 0 );
		instructions.emplace( name,std::make_unique<T>() );
	}
	void RegisterAlias( std::string main,std::string alias )
	{
		assert( instructions.count( alias ) == 0 );
		// find pair of main mne, get address of the Instruction from unique_ptr
		instructions.emplace( alias,std::make_unique<AliasInstruction>( 
			instructions.find( main )->second.get() ) );
	}
	template<class T>
	void RegisterDirective( std::string name )
	{
		assert( directives.count( name ) == 0 );
		directives.emplace( name,std::make_unique<T>() );
	}
private:
	std::ostream& mout = std::cout;
	std::map<std::string,Symbol> symbols;
	std::multimap<std::string,Symbol> symbolReferences;
	std::map<std::string,std::unique_ptr<Instruction>> instructions;
	std::map<std::string,std::unique_ptr<Directive>> directives;
	std::vector<std::pair<std::string,int>> sourceLines;
	std::vector<std::optional<unsigned char>> ram;
	static constexpr int ramsize = 256;
	int address = 0;
};