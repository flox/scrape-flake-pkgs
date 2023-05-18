# ============================================================================ #
#
#
#
# ---------------------------------------------------------------------------- #

{ stdenv, bash, nix, boost, nlohmann_json, darwin }: stdenv.mkDerivation {
  pname       = "scrape-flake-pkgs";
  version     = "0.1.0";
  src         = builtins.path { path = ./.; };
  buildInputs = [
    nix nix.dev boost nlohmann_json
  ] ++ (
    if stdenv.isDarwin then [darwin.apple_sdk.frameworks.Security] else []
  );
  propagatedBuildInputs = [bash nix];
  dontConfigure         = true;
  libExt                = stdenv.hostPlatform.extensions.sharedLibrary;
  buildPhase            = ''
    $CXX                                                                       \
      -shared                                                                  \
      -fPIC                                                                    \
      -std=c++17                                                               \
      -I${nix.dev}/include                                                     \
      -I${nix.dev}/include/nix                                                 \
      -I${boost.dev}/include                                                   \
      -I${nlohmann_json}/include                                               \
      -L${nix}/lib                                                             \
      -lnixutil                                                                \
      -lnixstore                                                               \
      -lnixcmd                                                                 \
      -lnixexpr                                                                \
      -lstdc++                                                                 \
      -o "libscrape$libExt"                                                    \
      ${if stdenv.isDarwin then "-undefined suppress -flat_namespace" else ""} \
      ./scrape.cc                                                              \
    ;
  '';
  installPhase = ''
    mkdir -p "$out/bin" "$out/libexec";
    mv "./libscrape$libExt" "$out/libexec/libscrape$libExt";
    cat <<EOF >"$out/bin/$pname"
    #! ${bash}/bin/bash
    # A wrapper around Nix that includes the \`libscrape' plugin.
    # First we add runtime executables to \`PATH', then pass off to Nix.
    for p in \$( <"$out/nix-support/propagated-build-inputs"; ); do
      if [[ -d "\$p/bin" ]]; then PATH="\$PATH:\$p/bin"; fi
    done
    exec "${nix}/bin/nix" --plugin-files "$out/libexec/libscrape$libExt"  \
                          scrape "\$@";
    EOF
    chmod +x "$out/bin/$pname";
  '';
}


# ---------------------------------------------------------------------------- #
#
#
#
# ============================================================================ #