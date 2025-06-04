In-line Scanners:
  * _under-emph_
  * *star-emph*
  * __under-strong__
  * **star-strong**
  * ___under-strong-emph___
  * ***star-strong-emph***
  * super^script^
  * sub~script~
  * ~~strikethrough~~
  * [text span]{.underline}
  * [web link](https://www.google.com)
  * `code block`
  * `` `literal looking` ``

When considering underscore and star syntax together, the parse_inline function
may need to consider symbols prior. (more maybe just backtrack?)
