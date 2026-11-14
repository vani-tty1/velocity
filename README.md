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

## Building from source

You can use the bash script [`start.sh`](start.sh) for automatically building and launching it.

For development you can point Velocity at one of the mock servers with
`VELOCITY_SERVER_URL` and `VELOCITY_SERVER_TYPE` (`librespeed`(the default) or
`ookla`) if the network is not available or is metered. `build-aux/mock-librespeed.py` and `
build-aux/mock-ookla.py` are local stand-ins.


## License

[![Large GPLv3 logo with “Free as in Freedom”](https://www.gnu.org/graphics/gplv3-with-text-136x68.png)](http://www.gnu.org/licenses/gpl-3.0.en.html)

```bash
Copyright 2026 Giovanni Rafanan

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
 
SPDX-License-Identifier: GPL-3.0-or-later
```
