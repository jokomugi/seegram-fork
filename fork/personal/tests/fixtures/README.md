# Session import fixtures

These archives contain synthetic, unusable all-zero authorization keys and
invented identities. No user session was used. Regenerate with `python3 generate.py`;
the independent fixture writer uses OpenSSL AES-ECB blocks to construct IGE data.

- `encrypted-tdata.zip`: legacy encryption, two accounts, cached names and one
  restricted profile. Main passcode: `fixture`; profile passcode: `profile`.
- `rotated-tdata.zip`: newer `key_data0` must take precedence over `key_data1`.
- `traversal.zip`: an unsafe path must reject the complete archive.
- `multiple-roots.zip`: two roots must require an explicit folder selection.
