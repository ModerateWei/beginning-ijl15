# World map rich-tooltip port

This port targets BeiDou.exe SHA-256:
1198fa57ca5a7c489bae43ec13c69681d9cabe0f96762f3dc0357facf2e7d4df

It adds the recovered 0x44-byte hotspot hit test, the three original client
hooks, WZ map/monster/NPC metadata loading, and a Canvas/Font/Layer tooltip.
The matching client has complete data for 1,129 of 1,152 eligible world-map
references (98.0%). Missing data uses an ID fallback.

Build Release x86 and test against the matching client. This repository build
does not replace or deploy the client's current ijl15.dll automatically.
