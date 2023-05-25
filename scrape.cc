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

#define _re_vp "(0|[1-9])[0-9]*"
static const std::regex semverRE(
  _re_vp "\\." _re_vp "\\." _re_vp "(-[-[:alnum:]_+.]+)?"
, std::regex::ECMAScript
);

static const std::unordered_set<std::string> defaultSystems = {
 "x86_64-linux", "aarch64-linux", "x86_64-darwin", "aarch64-darwin"
};


/* -------------------------------------------------------------------------- */

  static inline bool
isPkgsSubtree( std::string_view attrName )
{
  return ( attrName == "packages" ) || ( attrName == "legacyPackages" );
}


/* -------------------------------------------------------------------------- */

  static inline void
parseSystems( const std::string                     & str
            ,       std::unordered_set<std::string> & systems
            )
{
  if ( str != "" )
    {
      std::stringstream ss( str );
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


/* -------------------------------------------------------------------------- */

class FlakeCommand : virtual nix::Args, public nix::MixFlakeOptions
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

      nix::FlakeRef
    getFlakeRef()
    {
      return nix::parseFlakeRef( flakeUrl, nix::absPath( "." ) ); //FIXME
    }

      nix::flake::LockedFlake
    lockFlake()
    {
      return nix::flake::lockFlake( * this->getEvalState()
                                  , this->getFlakeRef()
                                  , lockFlags
                                  );
    }

      std::vector<std::string>
    getFlakesForCompletion() override
    {
      return { flakeUrl };
    }
};


/* -------------------------------------------------------------------------- */

struct CmdFlakeScrape : FlakeCommand {

  bool showAllSystems   = false;
  bool warnedAllSystems = false;
  std::string _systems  = "";

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

    std::string
  description() override
  {
    return "Scrape a flake's packages and legacyPackages outputs.";
  }

    std::string
  doc() override
  {
    return "Scrape a flake's packages and legacyPackages outputs.";
  }

    void
  run( nix::ref<nix::Store> store ) override
  {
    nix::evalSettings.enableImportFromDerivation.setDefault( false );

    auto state = getEvalState();
    auto flake = std::make_shared<nix::flake::LockedFlake>( lockFlake() );

    std::unordered_set<std::string> systems;
    std::unordered_set<std::string> packagesSystems;
    std::unordered_set<std::string> legacyPackagesSystems;

    if ( ! showAllSystems ) { parseSystems( _systems, systems ); }

    std::function<bool( const std::string & system )> shouldScrapeSystem;
    shouldScrapeSystem = [&]( const std::string & system ) -> bool
    {
      return showAllSystems || ( systems.find( system ) != systems.end() );
    };

    // For frameworks it's important that structures are as lazy as possible
    // to prevent infinite recursions, performance issues and errors that
    // aren't related to the thing to evaluate. As a consequence, they have
    // to emit more attributes than strictly (sic) necessary.
    // However, these attributes with empty values are not useful to the user
    // so we omit them.
    std::function<bool(
            nix::eval_cache::AttrCursor & visitor
    , const std::vector<nix::Symbol>    & attrPath
    , const nix::Symbol                 & attr
    )> hasContent;
    hasContent = [&](
              nix::eval_cache::AttrCursor & visitor
    ,   const std::vector<nix::Symbol>    & attrPath
    ,   const nix::Symbol                 & attr
    ) -> bool
    {
      auto attrPath2( attrPath );
      attrPath2.push_back( attr );

      auto attrPathS = state->symbols.resolve( attrPath2 );

      const auto & attrName = state->symbols[attr];

      auto visitor2 = visitor.getAttr( attrName );

      try
        {
          if ( isPkgsSubtree( attrPathS[0] ) &&
               ( attrPathS.size() == 1 || attrPathS.size() == 2 )
             )
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
        }
      catch ( nix::EvalError & e )
        {
          // Some attrs may contain errors, eg. legacyPackages of
          // nixpkgs. We still want to recurse into it, instead of
          // skipping it at all.
          return true;
        }
    };

    std::function<nlohmann::json(
            nix::eval_cache::AttrCursor & visitor
    , const std::vector<nix::Symbol>    & attrPath
    , const std::string                 & headerPrefix
    , const std::string                 & nextPrefix
    )> visit;

    visit = [&](
            nix::eval_cache::AttrCursor & visitor
    , const std::vector<nix::Symbol>    & attrPath
    , const std::string                 & headerPrefix
    , const std::string                 & nextPrefix
    ) -> nlohmann::json
    {
      auto j = nlohmann::json::object();

      auto attrPathS = state->symbols.resolve( attrPath );

      nix::Activity act(
        * nix::logger
      , nix::lvlInfo
      , nix::actUnknown
      , nix::fmt( "evaluating '%s'", concatStringsSep( ".", attrPathS ) )
      );

      try
        {
          auto recurse = [&]()
          {
            std::vector<nix::Symbol> attrs;
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

                bool last     = i + 1 == attrs.size();
                auto visitor2 = visitor.getAttr( attrName );
                auto attrPath2( attrPath );

                attrPath2.push_back( attr );

                auto j2 = visit(
                  * visitor2
                , attrPath2
                , nix::fmt( ANSI_GREEN "%s%s" ANSI_NORMAL ANSI_BOLD
                            "%s" ANSI_NORMAL
                          , nextPrefix
                          , last ? nix::treeLast : nix::treeConn
                          , attrName
                          )
                , nextPrefix + ( last ? nix::treeNull : nix::treeLine )
                );
                if ( attrPath.size() == 0 )
                  {
                    if ( isPkgsSubtree( attrName ) )
                      {
                        j.emplace( attrName, std::move( j2 ) );
                      }
                  }
                else if ( isPkgsSubtree( attrPathS[0] ) )
                  {
                    if ( attrPath.size() == 1 )
                      {
                        if ( shouldScrapeSystem( attrName ) )
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
          };

          auto showDerivation = [&]()
          {
            auto name = visitor.getAttr( state->sName )->getString();
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

            nix::DrvName dname( _name );

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

            if ( version )     { j.emplace( "version", * version ); }
            if ( description ) { j.emplace( "description", * description ); }
            if ( homepage )    { j.emplace( "homepage", * homepage ); }
            if ( license )     { j.emplace( "license", * license ); }
            if ( broken )      { j.emplace( "broken", * broken ); }
            if ( free )        { j.emplace( "free", * free ); }
            if ( position )    { j.emplace( "position", * position ); }
            if ( longDescription )
              {
                j.emplace( "longDescription", * longDescription );
              }

          };

        if ( ( attrPath.size() == 0 ) ||
             ( ( ( attrPath.size() == 1 ) || ( attrPath.size() == 2 ) ) &&
               ( attrPathS[0] == "packages" ) )
           )
          {
            if ( attrPath.size() == 2 )
              {
                packagesSystems.emplace( std::string( attrPathS[1] ) );
              }
            recurse();
          }
        else if ( ( attrPath.size() == 3 ) &&
                  ( attrPathS[0] == "packages" )
                )
          {
            if ( ! shouldScrapeSystem( attrPathS[1] ) )
              {
                if ( ! warnedAllSystems )
                  {
                    warnedAllSystems = true;
                    nix::logger->warn( nix::fmt(
                      "Some systems omitted (use '--all-systems' to show)"
                    ) );
                  }
              }
            else
              {
                if ( visitor.isDerivation() )
                  {
                    showDerivation();
                  }
                else
                  {
                    throw nix::Error( "expected a derivation" );
                  }
              }
          }
        else if ( ( 0 < attrPath.size() ) &&
                  ( attrPathS[0] == "legacyPackages" )
                )
          {
            if ( attrPath.size() == 1 )
              {
                recurse();
              }
            else if ( ! shouldScrapeSystem( attrPathS[1] ) )
              {
                legacyPackagesSystems.emplace(
                  std::string( attrPathS[1] )
                );
                if ( ! warnedAllSystems )
                  {
                    warnedAllSystems = true;
                    nix::logger->warn( nix::fmt(
                      "Some systems omitted (use '--all-systems' to show)"
                    ) );
                  }
              }
            else
              {
                if ( visitor.isDerivation() )
                  {
                    showDerivation();
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
                    recurse();
                  }
              }
          }
        }
      catch ( nix::EvalError & e )
        {
          if ( ! ( ( 0 < attrPath.size() ) &&
                   ( attrPathS[0] == "legacyPackages" ) )
             )
            {
              throw;
            }
        }

      return j;
    };

    auto cache = openEvalCache( * state, flake );

    auto j = visit(
               * cache->getRoot()
             , {}
             , nix::fmt( ANSI_BOLD "%s" ANSI_NORMAL, flake->flake.lockedRef )
             , ""
             );

    nix::logger->cout( "%s", j.dump() );
    // Unrolled form of `printInfo' macro from `nix/logging.hh'
    // Show these lists using `nix scrape -v ARGS;'
    if ( nix::lvlInfo <= nix::verbosity )
      {
        nix::logger->log(
          nix::lvlInfo
        , nix::fmt(
            "packagesSystems: [%s]\nlegacyPackagesSystems: [%s]"
          , nix::concatStringsSep( ", ", packagesSystems )
          , nix::concatStringsSep( ", ", legacyPackagesSystems )
          )
        );
      }

  }  /* End `CmdFlakeScrape::run(...)' */

};  /* End class `CmdFlakeScrape' */


/* -------------------------------------------------------------------------- */

static auto rScrapeCmd = nix::registerCommand<CmdFlakeScrape>( "scrape" );


/* -------------------------------------------------------------------------- *
 *
 *
 * ========================================================================== */
