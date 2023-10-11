{
  description = "CSA regex matcher implementation";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-23.05";
  };

  outputs = { self, nixpkgs }:
    let
      allSystems = [
        "x86_64-linux" # 64-bit Intel/AMD Linux
        "aarch64-linux" # 64-bit ARM Linux
        "x86_64-darwin" # 64-bit Intel macOS
        "aarch64-darwin" # 64-bit ARM macOS
      ];

      forAllSystems = f: nixpkgs.lib.genAttrs allSystems (system: f {
        pkgs = import nixpkgs { inherit system; };
      });
    in
    {
      packages = forAllSystems ({ pkgs }: {
        default =
          pkgs.stdenv.mkDerivation rec {
            name = "ca_cli";
            src = self;
            nativeBuildInputs = with pkgs; [ cmake ];
            cmakeFlags = [
              "-DCMAKE_CXX_COMPILER=g++"
            ];
            installPhase = ''
              mkdir -p $out/bin
              cp ${name} $out/bin
            '';
          };
      });
    };
}
