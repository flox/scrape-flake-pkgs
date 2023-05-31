/* ========================================================================== *
 *
 *
 * -------------------------------------------------------------------------- */

#include <iostream>
#include <string>
#include <regex>
#include <optional>


/* -------------------------------------------------------------------------- */

/* Matches Semantic Version strings, e.g. `4.2.0-pre' */
#define _re_vp "(0|[1-9][0-9]*)"
static const std::regex semverRE(
  _re_vp "\\." _re_vp "\\." _re_vp "(-[-[:alnum:]_+.]+)?"
, std::regex::ECMAScript
);

/* Coercively matches Semantic Version Strings, e.g. `v1.0-pre' */
static const std::regex semverCoerceRE(
  "(.*@)?[vV]?(0*([0-9]+)(\\.0*([0-9]+)(\\.0*([0-9]+))?)?(-[-[:alnum:]_+.]+)?)"
, std::regex::ECMAScript
);


/* -------------------------------------------------------------------------- */

  bool
isSemver( const std::string & version )
{
  return std::regex_match( version, semverRE );
}

  bool
isSemver( std::string_view version )
{
  std::string v( version );
  return std::regex_match( v, semverRE );
}


/* -------------------------------------------------------------------------- */

  std::optional<std::string>
coerceSemver( std::string_view version )
{
  std::string v( version );
  /* If it's already a match for a proper semver we're done. */
  if ( std::regex_match( v, semverRE ) )
    {
      return std::optional<std::string>( v );
    }

  /* Try try matching the coercive pattern. */
  std::cmatch cm;
  if ( ! std::regex_match( v.c_str(), cm, semverCoerceRE ) )
    {
      return std::nullopt;
    }

  for ( unsigned int i = 0; i < cm.size(); ++i )
    {
      std::cout << "[" << i << "]: " << cm[i] << std::endl;
    }

  /**
   * Capture Groups Example:
   *   [0]: foo@v1.02.0-pre
   *   [1]: foo@
   *   [2]: 1.02.0-pre
   *   [3]: 1
   *   [4]: .02.0
   *   [5]: 2
   *   [6]: .0
   *   [7]: 0
   *   [8]: -pre
   */

  v = cm[3].str() + ".";
  if ( 0 < cm[5].length() ) { v += cm[5].str() + "."; }
  else                      { v += "0."; }

  if ( 0 < cm[7].length() ) { v += cm[7].str(); }
  else                      { v += "0"; }

  if ( 0 < cm[8].length() ) { v += cm[8].str(); }

  return std::optional<std::string>( v );
}


/* -------------------------------------------------------------------------- */

  int
main( int argc, char * argv[], char ** envp )
{
  std::string v( argv[1] );
  std::optional<std::string> s = coerceSemver( v );
  std::cout << s.value_or( "FAIL" ) << std::endl;
  return 0;
}


/* -------------------------------------------------------------------------- *
 *
 *
 * ========================================================================== */
