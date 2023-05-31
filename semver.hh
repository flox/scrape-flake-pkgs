/* ========================================================================== *
 *
 *
 * -------------------------------------------------------------------------- */

#pragma once

#include <string>
#include <regex>
#include <optional>


/* -------------------------------------------------------------------------- */

bool isSemver( const std::string & version );
bool isSemver( std::string_view version );

bool isDate( const std::string & version );
bool isDate( std::string_view version );

bool isCoercibleToSemver( const std::string & version );
bool isCoercibleToSemver( std::string_view version );

/* -------------------------------------------------------------------------- */

std::optional<std::string> coerceSemver( std::string_view version );


/* -------------------------------------------------------------------------- *
 *
 *
 * ========================================================================== */
