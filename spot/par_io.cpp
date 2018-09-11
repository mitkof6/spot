#include "par_io.h"
#include "xo/serialization/char_stream.h"

namespace spot
{
	// TODO: make these parameters accessible
	const par_value default_std_factor = 0.1;
	const par_value default_std_minimum = 0.01;
	const par_value default_upper_boundaray = 1e12;
	const par_value default_lower_boundaray = -1e12;

	par_value par_io::get( const string& name, par_value mean, par_value std, par_value min, par_value max )
	{
		auto full_name = prefix() + name;

		// check if we already have a value for this name
		if ( auto v = try_get( full_name ) )
			return *v;

		// make a new parameter if we are allowed to
		return add( full_name, mean, std, min, max );
	}

	par_value par_io::get( const string& name, const prop_node& pn )
	{
		auto full_name = prefix() + name;

		// check if we already have a value for this name
		if ( auto val = try_get( full_name ) )
			return *val;

		// see if this is a reference to another parameter
		if ( !pn.get_value().empty() && pn.get_value().front() == '@' )
		{
			auto val = try_get( pn.get_value().substr( 1 ) );
			xo_error_if( !val, "Could not find " + pn.get_value() );
			return *val;
		}

		return add( full_name, pn );
	}

	spot::par_value par_io::add( const string& full_name, const prop_node& pn )
	{
		// check if the prop_node has children
		optional< par_value > mean, std, min, max;
		if ( pn.size() > 0 )
		{
			if ( pn.get< bool >( "is_free", true ) )
			{
				mean = pn.get_any< par_value >( { "mean", "init_mean" } );
				std = pn.get_any< par_value >( { "std", "init_std" } );
				min = pn.get< par_value >( "min", default_lower_boundaray );
				max = pn.get< par_value >( "max", default_upper_boundaray );
			}
			else return pn.get_any< par_value >( { "mean", "init_mean" } ); // is_free = 0, return mean
		}
		else
		{
			// parse the string, format mean~std[min,max]
			// TODO: use string_view instead of char_stream?
			char_stream str( pn.get_value().c_str() );
			while ( str.good() )
			{
				char c = str.peekc();
				if ( str.good() )
				{
					if ( c == '~' )
					{
						xo_error_if( std, "Standard deviation already set" );
						str.getc();
						str >> std;
					}
					else if ( c == '[' || c == '<' || c == '(' )
					{
						str.getc();
						str >> min;
						xo_error_if( str.getc() != ',', "Error parsing parameter '" + full_name + "': expected ','" );
						str >> max;
						char c2 = str.getc();
						if ( ( c == '[' && c2 != ']' ) || ( c == '<' && c2 != '>' ) || ( c == '(' && c2 != ')' ) )
							xo_error( "Error parsing parameter '" + full_name + "': opening bracket " + c + " does not match closing bracket " + c2 );
					}
					else // just a value, interpret as mean
					{
						xo_error_if( mean, "Error parsing parameter '" + full_name + "': mean already defined" );
						str >> mean;
					}
				}
			}
		}

		// do some sanity checking and fixing
		xo_error_if( min && max && ( *min > *max ), "Error parsing parameter '" + full_name + "': min > max" );
		xo_error_if( !mean && !std && !min && !max, "Error parsing parameter '" + full_name + "': no parameter defined" );
		xo_error_if( mean && !std && ( min || max ), "Error parsing parameter '" + full_name + "': min / max without std" );

		if ( mean && !std && !min && !max )
			return *mean; // just a value

		if ( std && !mean ) { // using ~value notation
			mean = std;
			std = xo::max( default_std_factor * abs( *mean ), default_std_minimum );
		}
		if ( !mean && min && max )
			mean = *min + ( *max - *min ) / 2;
		if ( !min ) min = default_lower_boundaray;
		if ( !max ) max = default_upper_boundaray;

		return add( full_name, *mean, *std, *min, *max );
	}

	spot::par_value par_io::try_get( const string& name, const prop_node& parent_pn, const string& key, const par_value& default_value )
	{
		if ( auto* pn = parent_pn.try_get_child( key ) )
			return get( name, *pn );
		else return default_value;
	}
}
