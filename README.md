<p align="center">
  <img src="https://emuleai.github.io/eMuleAI_banner.png" alt="eMule AI banner" />
</p>

# eMule AI

A modern fork of the classic open-source eMule P2P file sharing client.

## Overview

eMule AI modernizes the classic eMule experience with improvements focused on today's networks, hardware, and usability.

For feature overviews, installation help, migration guidance, settings reference, NAT Traversal documentation, release notes, and troubleshooting, visit the official documentation hub:

https://emuleai.github.io

## Downloads

For the latest builds and release notes, visit the GitHub Releases page:

https://github.com/eMuleAI/eMuleAI/releases

For Docker deployments, use the official emuleai/emuleai (https://hub.docker.com/r/emuleai/emuleai) image on Docker Hub. Linux and macOS users can use Xpra desktop integration while headless Linux systems can use noVNC browser access. See the [Docker Guide](#docker-guide) below for installation, configuration, networking, update, and troubleshooting instructions.

For installation and upgrade guidance, use the official documentation:

https://emuleai.github.io

## Docker Guide

The official eMule AI Docker image runs the Windows x64 release under Wine in a Linux `amd64` container. It provides two display modes:

- **Xpra**, the default mode, presents the eMule AI window as a separate desktop window on Linux or macOS. The interface is still rendered by the Windows application under Wine.
- **noVNC** provides the complete virtual desktop in a browser. It is the fallback mode and is suitable for headless Linux systems.

The official public image is available from Docker Hub:

https://hub.docker.com/r/emuleai/emuleai

Please see the Docker overview for more details:

https://emuleai.github.io/index.html#overview-docker

## Migration Warning

eMule AI can work with existing eMule configuration files, including `.ini`, `.dat`, and `.met` files.

However, `known.met` and `StoredSearches.met` use an updated structure in eMule AI. Once these files are updated by eMule AI, they are no longer backward compatible with the original eMule client.

Before migrating, back up your entire existing eMule configuration folder.

Although migration is supported, a clean configuration is generally recommended to benefit fully from modern defaults and newer tuning improvements.

## Documentation

The official documentation hub is the primary source for installation help, migration guidance, settings reference, release notes, and troubleshooting:

https://emuleai.github.io

## Optional External Resources

Some features require user supplied external files such as server.met, nodes.dat, ipfilter.dat.

If you already have these files, copy them to the eMule AI config directory and restart the application.

You can also load server.met from Servers -> Update server.met from URL, nodes.dat from Kad -> Nodes.dat from URL, and ipfilter.dat or ipfilter.zip from Options -> Security -> IP Filter -> Update from URL.

Please use only sources you trust and review their origin, terms, license conditions, and legal status before use.

## Legal Notice

eMule AI is a general purpose peer to peer networking application intended for lawful use only, including authorized file distribution, archival purposes, research, and community sharing.

The project does not endorse, encourage, or promote copyright infringement or any other unlawful activity.

Users are solely responsible for how they use the software, including the files, metadata, network resources, and third-party services they choose to access, share, or download, and for complying with all applicable laws and regulations in their jurisdiction.

This software is provided "as is", without warranty of any kind, to the maximum extent permitted by applicable law.

## Distribution Notes

This repository and its release packages provide the software and related documentation.

External resources such as server lists, node lists, IP filter lists, geolocation databases, or other third-party data sources, where supported by the software, are separate from the software and may be subject to independent licenses, terms, availability, or restrictions imposed by their respective providers.

eMule AI does not provide or endorse any specific third-party content source for unlawful use. If users choose to configure external resources, they are solely responsible for reviewing their origin, license terms, and legal status before use.

Source code for released versions is available in the GitHub repository:

https://github.com/eMuleAI/eMuleAI

## Support

For documentation and user guidance:

https://emuleai.github.io

To report bugs, request features, or follow development, visit the GitHub repository:

https://github.com/eMuleAI/eMuleAI

## Acknowledgements

Many thanks to everyone who contributed to eMule over the years. Without their work, eMule AI would not have been possible.

**Developers:** Merkur, John aka. Unknown1, Ornis, Bluecow, Tecxx, Pach2, Juanjo, Dirus, Barry, zz, Some Support

**Modders:** David Xanatos, Stulle, XMan, netfinity, WiZaRd, leuk_he, enkeyDev, SLUGFILLER, SiRoB, khaos, fox88, brand33d, Enig123, TAHO, Pretender, Mighty Knife, Ottavio84, Dolphin, sFrQlXeRt, evcz, cyrex2001, zz_fly, Slaham, Spike, shadow2004, gomez82, JvA, Pawcio, lovelace, MoNKi, Avi3k, Commander, emulEspaña, Maella, VQB, J.C.Conner

**Testers:** fangguihua1995, QICKV8, lzk87, Heliotropo, Havokdan, Andrey23, mistressadmin, enone, AnneDane, edelkas, Aokromes, superlent, tchara, OConnell, potes31, lapollarecord, tictoc9, arturx, Sony, Monk, Myxin, Mr Ozon, Daan, Elandal, Frozen_North, kayfam, Khandurian, Masta2002, mrLabr, Nesi-San, SeveredCross, Skynetman

## License

eMule AI is free software. You may redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 2 of the License or, at your option, any later version.

This software is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY, including without limitation the implied warranties of MERCHANTABILITY and FITNESS FOR A PARTICULAR PURPOSE.

A copy of the GNU General Public License is included in this package at `Licenses/license.txt`.

Third-party license notices and additional license texts are included in the `Licenses` directory, including `Licenses/THIRD-PARTY-NOTICES.txt`.
