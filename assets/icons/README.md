Application icons are embedded at build time. The frontend projects use `ApplicationIcon`; the firewall and client backend Makefiles compile `app.rc` with MinGW `windres`.

`build.sh` generates the server backend resource before building. For a manual server build, run `GOOS=windows GOARCH=amd64 go generate ./cmd` from `server/backend` first. Regenerate after changing the server backend icon; the generated `.syso` file is ignored by Git.
