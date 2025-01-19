# Dynamics Wrangler

**Dynamics Wrangler** (*dyngler*) is yet another replacement for [*patchelf*](https://github.com/NixOS/patchelf) and [*chrpath*](https://github.com/openEuler-BaseService/chrpath), with less power but more reliable and consistent (and it doesn't triple the size of your binary when you process it :>).

It's features are the following:

- Replacement of needed dependencies.
- Modification of the "soname".
- Modification of the run-time path ("rpath").
- Changing from "rpath" to "runpath" (and the opposite) to set the priority.
- Querying various dynamics properties (needed, soname, missing dependencies, etc).
- Finding automatically new name of missing dependencies (via the ld.cache).

Patching strings in an already compiled ELF files have a limitation: it's **impossible to replace a string with one longer than the original one, only shorter**!

## Building

Building *dyngler* can be done using GNU Make:

```
make
```

## Install

To install *dyngler*, run the following target:

```
make install PREFIX=(prefix)
```

The variable `PREFIX` defaults to `/usr/local`.

## Uninstall

To uninstall *dyngler*, run the following target using the same prefix as specified in the install process:

```
make uninstall PREFIX=(prefix)
```
