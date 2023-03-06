# slcl, a suckless cloud

`slcl` is a simple and fast implementation of a web file server, commonly
known as "cloud storage" or simply "cloud", written in C99.

## Disclaimer

While `slcl` might not share some of the philosophical views from the
[suckless project](https://suckless.org), it still strives towards minimalism,
simplicity and efficiency.

## Features

- Private access directory with file uploading.
- Read-only public file sharing.
- Its own, tiny HTTP/1.0 and 1.1-compatible server.
- A simple JSON file as the credentials database.
- No JavaScript.

### TLS

In order to maintain simplicity and reduce the risk for security bugs, `slcl`
does **not** implement TLS support. Instead, this should be provided by a
reverse proxy, such as [`caddy`](https://caddyserver.com/).

### Root permissions

`slcl` is expected to listen to connections from any port number so that `root`
access is not required. So, in order to avoid the risk for security bugs,
**please do not run `slcl` as `root`**.

### Encryption

Since no client-side JavaScript is used, files are **uploaded unencrypted**
to `slcl`. If required, encryption should be done before uploading e.g.: using
[`gpg`](https://gnupg.org/).

## Requirements

- A POSIX environment.
- OpenSSL >= 3.0.
- cJSON >= 1.7.15.
- [`dynstr`](https://gitea.privatedns.org/xavi92/dynstr)
(provided as a `git` submodule).
- `xxd` (for [`usergen`](usergen) only).
- CMake (optional).

### Ubuntu / Debian

```sh
sudo apt install libcjson-dev libssl-dev xxd
```

## How to use
### Build

Two build environments are provided for `slcl` - feel free to choose any of
them:

- A mostly POSIX-compliant [`Makefile`](/Makefile).
- A [`CMakeLists.txt`](/CMakeLists.txt).

`slcl` can be built using the standard build process:

#### Make

```sh
$ make
```

#### CMake

```sh
$ mkdir build/
$ cmake ..
$ cmake --build .
```

### Setting up

`slcl` consumes a path to a directory with the following tree structure:

```
.
├── db.json
├── public
└── user
```

Where:

- `db.json` is the credentials database. Details are explained below.
    - **Note:** `slcl` creates a database with no users if not found, with
    file mode bits set to `0600`.
- `public` is a directory containing read-only files that can be accessed
without authentication. Internally, they are implemented as simlinks to
other files.
    - **Note:** this directory must be created before running `slcl`.
- `user` is a directory containing user directories, which in turn contain
anything users put into them.
    - **Note:** this directory must be created before running `slcl`.

A more complete example:

```
.
├── db.json
├── public
│   └── 416d604c03a1cbb2 -> user/alice/file.txt
└── user
    ├── alice
    │   └── file.txt
    └── john
        └── file2.txt
```

**Note:** user directories (`alice` and `john` on the example above) must be
created before running `slcl`.

### Credentials database

`slcl` reads credentials from the `db.json` database, with the following
schema:

```json
{
    "users": [{
        "name":	"...",
        "password":	"...",
        "salt":	"...",
        "key":	"..."
    }]
}
```

[`usergen`](usergen) is an interactive script that consumes a username and
password, and writes a JSON object that can be appended to the `users` JSON
array in `db.json`. A salt is randomly generated using `openssl` and passwords
are hashed multiple times beforehand - see [`usergen`](usergen) and
[`auth.c`](/auth.c) for further reference. Also, a random key is generated
that is later used to sign HTTP cookies.

When users authenticate from a web browser, `slcl` sends a SHA256HMAC-signed
[JSON Web Token](https://jwt.io), using the random key generated by
[`usergen`](usergen). No session data is kept on the server.

### Running

To run `slcl`, simply run the executable with the path to a directory including
the files listed above. By default, `slcl` will listen to incoming connections
on a random TCP port number. To set a specific port number, use the `-p`
command line option. For example:

```sh
slcl -p 7822 ~/my-db/
```

## Why this project?

Previously, I had been recommended Nextcloud as an alternative to proprietary
services like Dropbox. Unfortunately, despite being a very flexible piece of
software, Nextcloud is _way too_ heavy on resources, specially on lower end
hardware such as the Raspberry Pi 3:

- It uses around **30%** RAM on my Raspberry Pi 3, configured with 973 MiB of
RAM, and of course it gets worse with several simultaneous users.
- Simple operations like searching and previewing files cause large amounts
of I/O and RAM usage, so much that it locks the whole server up more often than
not.
- Nextcloud pages are bloated. Even the login page is over **15 MiB** (!).
- Requires clients to run JavaScript, which also has a significant performance
penalty on the web browser. Also, some users do not feel comfortable running
JavaScript from their web browsers, and thus prefer to disable it.

After years of recurring frustration as a Nextcloud administrator and user,
I looked for alternatives that stripped out most of the unneeded bloat from
Nextcloud, while providing the required features listed above. However,
I could not find any that fit them, so I felt challenged to design a new
implementation.

On the other hand, command line-based solutions like `rsync` might not be as
convenient for non-technical people, compared to a web browser, or might not
be even available e.g.: phones.

## License

```
slcl, a suckless cloud.
Copyright (C) 2023  Xavier Del Campo Romero

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
```

Also, see [`LICENSE`](/LICENSE).
