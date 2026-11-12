<div align="start">
<img src="data/icons/hicolor/scalable/apps/io.github.vani_tty1.velocity.svg" alt="velocity logo" width="128"/>
</div>

[![Build Bundle](https://github.com/vani-tty1/velocity/actions/workflows/flatbuild-build.yaml/badge.svg)](https://github.com/vani-tty1/velocity/actions/workflows/flatbuild-build.yaml)

[![Copr build status](https://copr.fedorainfracloud.org/coprs/vaniiiiii/velocity/package/velocity/status_image/last_build.png)](https://copr.fedorainfracloud.org/coprs/vaniiiiii/velocity/package/velocity/)

# velocity

A speedtest app using [librespeed](https://librespeed.org/) and
[Ookla](https://www.speedtest.net/) (speedtest.net) servers.

>Ookla support is unofficial: it uses the same public server directory and
HTTP endpoints as `speedtest-cli`.

## Install

>NOTE: You can install the latest build via the artifacts in the latest
workflow run in the [Github Actions](https://github.com/vani-tty1/velocity/actions) page

[![Packaging status](https://repology.org/badge/vertical-allrepos/velocity-speedtest.svg)](https://repology.org/project/velocity-speedtest/versions)

### Building from source

You can use the bash script [`start.sh`](start.sh) for automatically building and launching it.

For development you can point Velocity at one of the mock servers with
`VELOCITY_SERVER_URL` and `VELOCITY_SERVER_TYPE` (`librespeed`(the default) or
`ookla`) if the network is not available or is metered. `build-aux/mock-librespeed.py` and `
build-aux/mock-ookla.py` are local stand-ins.

