# WASMachine Core Component

[![Component Registry](https://components.espressif.com/components/espressif/wasmachine_core/badge.svg)](https://components.espressif.com/components/espressif/wasmachine_core)

This component provides core functionality for the WASMachine project. For details of Espressif's WASMachine project, please refer to the [ESP-WASMachine documentation](https://github.com/espressif/esp-wasmachine/blob/master/README.md). It's recommended to read the documentation carefully before using this component.

## Add component to your project
Please use the component manager command add-dependency to add the wasmachine_core component to your project's dependency, during the CMake step the component will be downloaded automatically.

```shell
idf.py add-dependency "espressif/wasmachine_core=*"
```

## Examples
Please use the component manager command create-project-from-example to create the project from example template.

```shell
idf.py create-project-from-example "espressif/wasmachine_core=*:wasmachine"
```

Then the example will be downloaded in current folder, you can check into it for build and flash.
