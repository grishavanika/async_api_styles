call pandoc -s --toc --toc-depth=4 ^
  --standalone ^
  --number-sections ^
  --highlight=kate ^
  --from markdown --to=html5 ^
  --lua-filter=pandoc-anchor-links.lua ^
  async_api_styles.md ^
  -o async_api_styles.html

