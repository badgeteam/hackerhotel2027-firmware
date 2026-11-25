# Security

## Report a vulnerability

Do not open a public issue for a security problem. Report it privately
through GitHub instead:

1. Go to the repository's **Security** tab.
2. Click **Report a vulnerability**.

The direct link is
<https://github.com/badgeteam/hackerhotel2027-firmware/security/advisories/new>.
Only the maintainers can read the report. We discuss the fix with you in
the draft advisory, and publish the advisory when the fix is on `main`.

Please include:

- what the problem is, and what an attacker can do with it;
- the steps to reproduce it, or a proof of concept;
- the commit you tested. The web interface shows its own commit in its
  footer, and the badge's firmware commit in the **Firmware** panel.

This is a volunteer project, so an answer can take a few days.

## Supported versions

Only `main` is supported. We fix problems there, and you update a badge
with the firmware from `main` (see `README.md`, "Flash over USB", or the
web interface's **Firmware** panel). There are no release branches.

## Scope

In scope:

- **The firmware's USB protocol** (`webusb_protocol.c`,
  `webusb_protocol.h`, `fsusb.c`): a command from the host that crashes
  the badge, corrupts the item store or the persistent record, or reads
  or writes memory that it should not.
- **The web interface** (`webui/`, at <https://badge.team/hackerhotel2027-tool/>,
  and for testing at <https://scene.rs/star/>):
  for example script injection through a file, an item name or a text
  that the page shows.
- **The app loader** (`app_host.c`, `apps/hhapp.h`): a `.hhapp` whose
  header passes the checks but makes the firmware read or write outside
  the app's RAM before the app runs.
- **The infrared receiver** (`infrared.c`, `ir_codec.c`, `greet.c`): a
  frame or packet from another badge or a remote that crashes the badge or
  corrupts its memory.

Out of scope:

- **What an app does once it runs.** An app is native RISC-V code. It runs
  with the same access to the chip as the firmware, and nothing isolates
  it. Only add apps that you trust.
- **Flashing firmware from the USB host.** Whoever controls the USB host
  that the badge is connected to can flash any firmware. The `BOOT_ROM`
  command (0x40) or the menu's **UPDATE** entry starts the boot ROM, with
  no button press for the command. That is how updates work. So a host
  program, or a web page that you gave WebUSB access to the badge, can
  replace the firmware. This is out of scope. Only connect the badge to
  hosts and pages that you trust.
- **Problems in `ch32fun/`** that do not affect this firmware. Report
  those to [ch32fun](https://github.com/cnlohr/ch32fun).
