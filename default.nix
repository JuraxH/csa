{ stdenv, cmake }:

stdenv.mkDerivation rec {
  name = "csa_bench";
  src = ./.;

  nativeBuildInputs = [ 
    cmake
  ];

  configurePhase = ''
    cmake .
  '';

  buildPhase = ''
    make
  '';

  installPhase = ''
    mkdir -p $out/bin
    cp csa_bench $out/bin
  '';

}

