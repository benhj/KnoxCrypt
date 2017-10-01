
KnoxCrypt (formerly TeaSafe): An encrypted filesystem
-----------------------------------------------------

###### What is it?

- A tool for creating and browsing encrypted 'boxes' of data; similar to Truecrypt. 
- Supports lots of ciphers including AES-256. 
- Utilizes a million iterations of PBKDF2 for key derivation. Seems like a big number but probably overkill.
- Can create sparse containers.
- Sub-volume capability.

###### What's with the name?

~~The name has stuck for historical reasons: a very early version used the XTEA cipher for encryption. I think the project could do with a better name though. Let me know if you have any suggestions.~~ I renamed the project to knoxcrypt. 

###### Caveats

KnoxCrypt is highly developmental and therefore probably buggy. I make no guarentees as to the integrity of stored data. Neither do I guarantee 100% data security. Having said that, if you're happy with the strength of AES-256 in CTR mode and with a key that has been derived using quite a few rounds of PBKDF2, then I think it should be fine. Take that as you will.

### Compiling

Note, only tested on Linux and Mac. With a bit of work, will probably build (sans fuse-bits) on windows
too. 

Requirements:
 
- some of the boost headers and libraries to build (see makefile).
- fuse for the main fuse layer binary (the binary 'knoxcrypt')
- crypto++ headers and libraries for building and linking
- ~~cryptostreampp, a small set of headers allowing straight forward implementation of encrypted file streams (see [https://github.com/benhj/cryptostreampp](https://github.com/benhj/cryptostreampp)).~~ This is now a submodule and is automatically grabbed when cloning knoxcrypt recursively.

First grab the source by cloning recursively:

<pre>
git clone --recursive https://github.com/benhj/knoxcrypt
</pre>

If you don't have fuse installed, you'll probably want to only build the main 
knoxcrypt library (libknoxcrypt.a), the shell (teashell) and makeknoxcrypt, the binary
used to make knoxcrypt containers. To build these, respectively:
<pre>
make lib
make shell
make makeknoxcrypt
</pre>
Note that building either of the binaries `teashell` or `makeknoxcrypt` will automatically build 
libknoxcrypt.a first.

`make` or `make all` will compile everything, i.e., the following binaries:

<pre>
test           : unit tests various parts of the main api
makeknoxcrypt  : builds knoxcrypt containers
knoxcrypt      : fuse layer used for mounting knoxcrypt containers
teashell       : shell utility used for accessing and modifying knoxcrypt containers
</pre>

To build a KnoxCrypt container that uses AES256, with 4096 * 128000 bytes, use the `makeknoxcrypt` binary:

<pre>
./makeknoxcrypt ./test.bfs 128000
</pre>

For alternative ciphers, use the `--cipher` flag, e.g.:

<pre>
./makeknoxcrypt ./test.bfs 128000 --cipher twofish
</pre>

The available cipher options are `aes`, `serpent`, `cast256`, `rc6`, `twofish`, `mars`, `camellia`, `rc5`, `shacal2` and `null`. Update 30/5/15: There are quite a few more than that these days. Have a look at the cryptostream headers if you're so inclined.

Note that `null` disables encryption and thus provides no security. The default is aes.

Sparse containers can also be created, growing in size as more data are written to them. Just use the `--sparse` flag during creation, i.e.:

<pre>
./makeknoxcrypt ./test.bfs 128000 --sparse 1
</pre>

Now to mount it to `/testMount` via fuse, use the `knoxcrypt` binary:

<pre>
./knoxcrypt ./test.bfs /testMount
</pre>

Runs the interactive shell on it using the `teashell` binary:

<pre>
./teashell ./test.bfs
</pre>

Licensing
---------

KnoxCrypt follows the BSD 3-Clause licence. 


