call pandoc -s --toc --toc-depth=4 ^
  --standalone ^
  --number-sections ^
  --syntax-highlighting=kate ^
  --from markdown ^
  --lua-filter=pandoc-anchor-links.lua ^
  --variable colorlinks=true ^
  --variable linkcolor=blue ^
  async_api.md ^
  -o async_api.pdf
