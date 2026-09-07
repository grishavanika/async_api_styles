call pandoc -s --toc --toc-depth=4 ^
  --standalone ^
  --number-sections ^
  --syntax-highlighting=kate ^
  --from markdown ^
  --lua-filter=pandoc-anchor-links.lua ^
  async_api.md ^
  -o async_api.pdf
