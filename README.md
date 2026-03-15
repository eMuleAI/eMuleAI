<p align="center">
  <img src="https://emuleai.github.io/eMuleAI_banner.png" alt="eMule AI banner" />
</p>

# eMule AI

A modern fork of the classic open-source eMule P2P file sharing client.

## Overview

eMule AI modernizes the classic eMule experience with improvements focused on today's networks, hardware, and usability.

For feature overviews, installation help, migration guidance, settings reference, NAT Traversal and uTP documentation, release notes, and troubleshooting, visit the official documentation hub:

https://emuleai.github.io

## Downloads

For the latest builds and release notes, visit the GitHub Releases page:

https://github.com/eMuleAI/eMuleAI/releases

For installation and upgrade guidance, use the official documentation:

https://emuleai.github.io

## Migration Warning

eMule AI can work with existing eMule configuration files, including `.ini`, `.dat`, and `.met` files.

However, `known.met` and `StoredSearches.met` use an updated structure in eMule AI. Once these files are updated by eMule AI, they are no longer backward compatible with the original eMule client.

Before migrating, back up your entire existing eMule configuration folder.

Although migration is supported, a clean configuration is generally recommended to benefit fully from modern defaults and newer tuning improvements.

## Documentation

The official documentation hub is the primary source for installation help, migration guidance, settings reference, release notes, and troubleshooting:

https://emuleai.github.io

## Optional External Resources

Some features require user-supplied external files such as server.met, nodes.dat, ipfilter.dat, or GeoLite2-City.mmdb.

If you already have these files, copy them to the eMule AI config directory and restart the application.

You can also load server.met from Servers -> Update server.met from URL, nodes.dat from Kad -> Nodes.dat from URL, and ipfilter.dat or ipfilter.zip from Options -> Security -> IP Filter -> Update from URL.

GeoLite2-City.mmdb must be downloaded separately from MaxMind at %s and is subject to MaxMind's terms. After copying it to the eMule AI config directory, enable GeoLite2 in Settings and restart the application.

Please use only sources you trust and review their origin, terms, license conditions, and legal status before use.

## Legal Notice

eMule AI is a general-purpose peer-to-peer networking application intended for lawful use only, including authorized file distribution, archival purposes, research, and community sharing.

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

**Developers:** Merkur, John aka. Unknown1, Ornis, Bluecow, Tecxx, Pach2, Juanjo, Dirus, Barry, zz, Some Support, fox88

**Modders:** David Xanatos, Stulle, XMan, netfinity, WiZaRd, leuk_he, enkeyDev, SLUGFILLER, SiRoB, khaos, Enig123, TAHO, Pretender, Mighty Knife, Ottavio84, Dolphin, sFrQlXeRt, evcz, cyrex2001, zz_fly, Slaham, Spike, shadow2004, gomez82, JvA, Pawcio, lovelace, MoNKi, Avi3k, Commander, emulEspaña, Maella, VQB, J.C.Conner

**Testers:** Sony, Monk, Myxin, Mr Ozon, Daan, Elandal, Frozen_North, kayfam, Khandurian, Masta2002, mrLabr, Nesi-San, SeveredCross, Skynetman

## License

eMule AI is free software. You may redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 2 of the License or, at your option, any later version.

This software is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY, including without limitation the implied warranties of MERCHANTABILITY and FITNESS FOR A PARTICULAR PURPOSE.

A copy of the GNU General Public License is included in this package at `Licenses/license.txt`.

Third-party license notices and additional license texts are included in the `Licenses` directory, including `Licenses/THIRD-PARTY-NOTICES.txt`.
