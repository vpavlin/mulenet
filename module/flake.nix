{
  description = "MuleNet desktop view: pure-QML over the mulenet_core module";

  inputs = {
    # The SAME builder rev as mulenet_core, so the view, core and host share one SDK.
    logos-module-builder.url = "github:logos-co/logos-module-builder/2b59cb8e855894f7e7a064b15bfae409f288080b";
    mulenet_core.url = "path:../mulenet_core";
    mulenet_core.inputs.logos-module-builder.follows = "logos-module-builder";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
