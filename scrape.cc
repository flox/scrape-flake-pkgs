/* ========================================================================== *
 *
 *
 * -------------------------------------------------------------------------- */

#include <bits/stdc++.h>
#include <nix/config.h>
#include <nix/command.hh>
#include <nix/eval.hh>
#include <nix/eval-cache.hh>
#include <nix/names.hh>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <regex>


/* -------------------------------------------------------------------------- */

using json = nlohmann::json;

namespace nix {
  namespace flake {


/* -------------------------------------------------------------------------- */

#define _re_vp "(0|[1-9])[0-9]*"
static const std::regex semverRE( _re_vp "\\." _re_vp "\\." _re_vp
                                  "(-[-[:alnum:]_+.]+)?"
                                , std::regex::ECMAScript);

static const std::unordered_set<std::string> defaultSystems = {
 "x86_64-linux", "aarch64-linux", "x86_64-darwin", "aarch64-darwin"
};


/* -------------------------------------------------------------------------- */

class FlakeCommand : virtual Args, public MixFlakeOptions
{
  std::string flakeUrl = ".";

  public:

    FlakeCommand()
      {
        expectArgs( {
          .label = "flake-url",
          .optional = true,
          .handler = { & flakeUrl },
          .completer = { [&]( size_t, std::string_view prefix ) {
            completeFlakeRef( getStore(), prefix );
          } }
        } );
      }

    FlakeRef getFlakeRef()
      {
        return parseFlakeRef( flakeUrl, absPath( "." ) ); //FIXME
      }

    LockedFlake lockFlake()
      {
        return flake::lockFlake( * getEvalState(), getFlakeRef(), lockFlags );
      }

    std::vector<std::string> getFlakesForCompletion() override
      {
        return { flakeUrl };
      }
};


/* -------------------------------------------------------------------------- */

struct CmdFlakeScrape : FlakeCommand {

  bool showAllSystems   = false;
  bool warnedAllSystems = false;
  std::string _systems  = "";

  std::unordered_set<std::string> systems;
  std::unordered_set<std::string> packagesSystems;
  std::unordered_set<std::string> legacyPackagesSystems;

  CmdFlakeScrape()
    {
      addFlag( {
        .longName    = "all-systems",
        .description = "Show the contents of outputs for all systems.",
        .handler     = { & showAllSystems, true }
      } );
      addFlag( {
        .longName    = "systems",
        .description = "Space separated list of systems to scrape.",
        .labels      = { "systems-list" },
        .handler     = { & _systems }
      } );
    }

  std::string description() override
    {
      return "Scrape a flake's packages and legacyPackages outputs.";
    }

  std::string doc() override
    {
      return "Scrape a flake's packages and legacyPackages outputs.";
    }

  void run( nix::ref<nix::Store> store ) override
    {
      evalSettings.enableImportFromDerivation.setDefault( false );

      auto state = getEvalState();
      auto flake = std::make_shared<LockedFlake>( lockFlake() );

      if ( ! showAllSystems )
        {
          if ( _systems != "" )
            {
              std::stringstream ss( _systems );
              std::string s;
              while ( getline( ss, s, ' ' ) )
                {
                  systems.emplace( s );
                }
            }
          else
            {
              systems = defaultSystems;
            }
        }

      auto cache = openEvalCache( * state, flake );

      auto j = visit(
                 * cache->getRoot()
               , {}
               , fmt( ANSI_BOLD "%s" ANSI_NORMAL, flake->flake.lockedRef )
               , ""
               );
      printInfo( fmt ( "packagesSystems: [%s]"
                     , concatStringsSep( ", ", packagesSystems ) ) );
      printInfo( fmt( "legacyPackagesSystems: [%s]"
                    , concatStringsSep( ", ", legacyPackagesSystems ) ) );
      logger->cout( "%s", j.dump() );
    }

  private:

    // For frameworks it's important that structures are as lazy as possible
    // to prevent infinite recursions, performance issues and errors that
    // aren't related to the thing to evaluate. As a consequence, they have
    // to emit more attributes than strictly (sic) necessary.
    // However, these attributes with empty values are not useful to the user
    // so we omit them.
      bool
    hasContent(       eval_cache::AttrCursor & visitor
              , const std::vector<Symbol>    & attrPath
              , const Symbol                 & attr
              )
    {
      auto state = getEvalState();
      auto attrPath2( attrPath );
      attrPath2.push_back( attr );
      auto attrPathS = state->symbols.resolve( attrPath2 );
      const auto & attrName = state->symbols[attr];

      auto visitor2 = visitor.getAttr( attrName );

      try {
          if ( ( attrPathS[0] == "legacyPackages" ||
                 attrPathS[0] == "packages" )
              && ( attrPathS.size() == 1 || attrPathS.size() == 2 ) )
            {
              for ( const auto & subAttr : visitor2->getAttrs() )
                {
                  if ( hasContent( * visitor2, attrPath2, subAttr ) )
                    {
                      return true;
                    }
                }
              return false;
            }
          // If we don't recognize it, it's probably content
          return true;
      } catch ( EvalError & e ) {
          // Some attrs may contain errors, eg. legacyPackages of
          // nixpkgs. We still want to recurse into it, instead of
          // skipping it at all.
          return true;
      }
  }


/* -------------------------------------------------------------------------- */

      void
    recurse(       eval_cache::AttrCursor & visitor
           , const std::vector<Symbol>    & attrPath
           , const std::string            & nextPrefix
           ,       nlohmann::json         & j
           )
    {
      auto state = getEvalState();
      std::vector<Symbol> attrs;
      auto attrPathS = state->symbols.resolve( attrPath );

      for ( const auto & attr : visitor.getAttrs() )
        {
          if ( hasContent( visitor, attrPath, attr ) )
            {
              attrs.push_back( attr );
            }
        }

      for ( const auto & [i, attr] : enumerate( attrs ) )
        {
          const auto & attrName = state->symbols[attr];
          bool last = i + 1 == attrs.size();
          auto visitor2 = visitor.getAttr( attrName );
          auto attrPath2( attrPath );
          attrPath2.push_back( attr );
          auto j2 = visit( * visitor2, attrPath2,
              fmt( ANSI_GREEN "%s%s" ANSI_NORMAL ANSI_BOLD
                   "%s" ANSI_NORMAL
                 , nextPrefix, last ? treeLast : treeConn, attrName ),
              nextPrefix + ( last ? treeNull : treeLine ) );
          if ( attrPath.size() == 0 )
            {
              if ( ( attrName == "packages" ) ||
                   ( attrName == "legacyPackages" ) )
                {
                  j.emplace( attrName, std::move( j2 ) );
                }
            }
          else if ( ( attrPathS[0] == "packages" ) ||
                    ( attrPathS[0] == "legacyPackages" ) )
            {
              if ( attrPath.size() == 1 )
                {
                  if ( systems.find( attrName ) != systems.end() )
                    {
                      j.emplace( attrName, std::move( j2 ) );
                    }
                }
              else
                {
                  j.emplace( attrName, std::move( j2 ) );
                }
            }
        }
    }


/* -------------------------------------------------------------------------- */

      void
    showDerivation(       eval_cache::AttrCursor & visitor
                  , const std::vector<Symbol>    & attrPath
                  ,       nlohmann::json         & j
                  )
    {
      auto state     = getEvalState();
      auto attrPathS = state->symbols.resolve( attrPath );
      auto name      = visitor.getAttr( state->sName )->getString();
      std::optional<std::string> description;
      std::optional<std::string> longDescription;
      std::optional<std::string> homepage;
      std::optional<std::string> license;
      std::vector<std::string>   outputs;
      std::vector<std::string>   outputsToInstall;
      std::optional<bool>        broken;
      std::optional<bool>        free;
      std::optional<std::string> position;
      std::string                pname;
      std::optional<std::string> version;
      bool                       isSemver;
      std::string _name = visitor.getAttr( "name" )->getString();

      DrvName dname( _name );

      if ( auto aOutputs = visitor.maybeGetAttr( "outputs" ) )
        {
          outputs = aOutputs->getListOfStrings();
        }

      if ( auto aPname = visitor.maybeGetAttr( "pname" ) )
        {
          pname = aPname->getString();
        }
      else
        {
          pname = dname.name;
        }

      if ( auto aVersion = visitor.maybeGetAttr( "version" ) )
        {
          version = aVersion->getString();
        }
      else if ( ! dname.version.empty() )
        {
          version = dname.version;
        }

      isSemver = version && std::regex_match( * version, semverRE );

      if ( auto aMeta = visitor.maybeGetAttr( state->sMeta ) )
        {
          if ( auto aDescription =
                 aMeta->maybeGetAttr( state->sDescription ) )
            {
              description = aDescription->getString();
            }
          if ( auto aLongDescription =
                 aMeta->maybeGetAttr( "longDescription" ) )
            {
              longDescription = aLongDescription->getString();
            }
          if ( auto aHomepage =
                 aMeta->maybeGetAttr( "homepage" ) )
            {
              homepage = aHomepage->getString();
            }
          if ( auto aLicense = aMeta->maybeGetAttr( "license" ) )
            {
              if ( auto aLicenseId =
                     aLicense->maybeGetAttr( "spdxId" ) )
                {
                  license = aLicenseId->getString();
                }
              if ( auto aFree = aLicense->maybeGetAttr( "free" ) )
                {
                  free = aFree->getBool();
                }
            }
          if ( auto aOutputsToInstall =
                 aMeta->maybeGetAttr( "outputsToInstall" ) )
            {
              outputsToInstall =
                aOutputsToInstall->getListOfStrings();
            }
          else
            {
              for ( auto & o : outputs )
                {
                  outputsToInstall.push_back( o );
                  if ( o == "out" )
                    {
                      break;
                    }
                }
            }
          if ( auto aBroken = aMeta->maybeGetAttr( "broken" ) )
            {
              broken = aBroken->getBool();
            }
          if ( auto aPosition = aMeta->maybeGetAttr( "position" ) )
            {
              position = aPosition->getString();
            }
        }

      j.emplace( "type", "derivation" );
      j.emplace( "name", name );
      j.emplace( "system", std::string( attrPathS[1] ) );
      j.emplace( "outputs", outputs );
      j.emplace( "outputsToInstall", outputsToInstall );
      j.emplace( "pname", pname );
      j.emplace( "isSemver", isSemver );

      if ( version )
        {
          j.emplace( "version", * version );
        }
      if ( description )
        {
          j.emplace( "description", * description );
        }
      if ( longDescription )
        {
          j.emplace( "longDescription", * longDescription );
        }
      if ( homepage )
        {
          j.emplace( "homepage", * homepage );
        }
      if ( license )
        {
          j.emplace( "license", * license );
        }
      if ( broken )
        {
          j.emplace( "broken", * broken );
        }
      if ( free )
        {
          j.emplace( "free", * free );
        }
      if ( position )
        {
          j.emplace( "position", * position );
        }
    }


/* -------------------------------------------------------------------------- */

      nlohmann::json
    visit(       eval_cache::AttrCursor & visitor
         , const std::vector<Symbol>    & attrPath
         , const std::string            & headerPrefix
         , const std::string            & nextPrefix
         )
    {
      nlohmann::json j = nlohmann::json::object();
      auto state       = getEvalState();
      auto attrPathS   = state->symbols.resolve( attrPath );

      Activity act( * logger, lvlInfo, actUnknown,
          fmt( "evaluating '%s'", concatStringsSep( ".", attrPathS ) ) );
      try {
        this->recurse( visitor, attrPath, nextPrefix, j );

        if ( ( attrPath.size() == 0 ) ||
             ( ( ( attrPath.size() == 1 ) || ( attrPath.size() == 2 ) ) &&
               ( attrPathS[0] == "packages" ) ) )
          {
            if ( attrPath.size() == 2 )
              {
                packagesSystems.emplace( std::string( attrPathS[1] ) );
              }
            recurse( visitor, attrPath, nextPrefix, j );
          }
        else if ( ( attrPath.size() == 3 ) &&
                  ( attrPathS[0] == "packages" ) )
          {
            if ( ( ! showAllSystems ) &&
                 ( systems.find( std::string( attrPathS[1] ) ) ==
                   systems.end() ) )
              {
                if ( ! warnedAllSystems )
                  {
                    warnedAllSystems = true;
                    logger->warn( fmt(
                      "Some systems omitted (use '--all-systems' to show)"
                    ) );
                  }
              }
            else
              {
                if ( visitor.isDerivation() )
                  {
                    showDerivation( visitor, attrPath, j );
                  }
                else
                  {
                    throw Error( "expected a derivation" );
                  }
              }
          }
        else if ( ( 0 < attrPath.size() ) &&
                  ( attrPathS[0] == "legacyPackages" ) )
          {
            if ( attrPath.size() == 1 )
              {
                recurse( visitor, attrPath, nextPrefix, j );
              }
            else if ( ( ! showAllSystems ) &&
                      ( systems.find( std::string( attrPathS[1] ) ) ==
                        systems.end() ) )
              {
                legacyPackagesSystems.emplace(
                  std::string( attrPathS[1] )
                );
                if ( ! warnedAllSystems )
                  {
                    warnedAllSystems = true;
                    logger->warn( fmt(
                      "Some systems omitted (use '--all-systems' to show)"
                    ) );
                  }
              }
            else
              {
                if ( visitor.isDerivation() )
                  {
                    showDerivation( visitor, attrPath, j );
                  }
                else if ( attrPath.size() <= 2 )
                  {
                    if ( attrPath.size() == 2 )
                      {
                        legacyPackagesSystems.emplace(
                          std::string( attrPathS[1] )
                        );
                      }
                    // FIXME: handle recurseIntoAttrs
                    recurse( visitor, attrPath, nextPrefix, j );
                  }
              }
          }
      } catch ( EvalError & e ) {
        if ( ! ( ( 0 < attrPath.size() ) &&
                 ( attrPathS[0] == "legacyPackages" ) ) )
          {
            throw;
          }
      }

      return j;
    }


/* -------------------------------------------------------------------------- */

};  /* End class `nix::flake::CmdFlakeScrape` */

/* -------------------------------------------------------------------------- */

  }  /* End namespace `nix::flake' */

}  /* End namespace `nix' */


/* -------------------------------------------------------------------------- */

static auto rScrapeCmd =
  nix::registerCommand<nix::flake::CmdFlakeScrape>( "scrape" );


/* -------------------------------------------------------------------------- *
 *
 *
 * ========================================================================== */
