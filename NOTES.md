# 1. NOTES

## 1.1. On the topic of characters, encodings, string formats
- Using `wchar_t` and `wstring` is inherently bad. [It turns out that the `wchar_t` isn't fixed length.](https://en.cppreference.com/w/cpp/language/types) In Windows it is 16 bits per "character" and in Linux it's mostly 32 bits.
- Using char16_t doesn't fix the problem since using Unicode means the idea of a "character" is that they are mapped to different code points - and combinations of those! Those are called "[grapheme clusters](http://mathias.gaunard.com/unicode/doc/html/unicode/introduction_to_unicode.html#unicode.introduction_to_unicode.grapheme_clusters)" and so even in UTF-32 encoding could use multiple code units as a single grapheme.
- Since `char32_t` still has the same problems and besides it's very wasteful on the memory, the less evil method is to stick with `char_t` and `std::string` and use UTF-8 as encoding.
- [This reasoning is mostly based on this blog post](https://ohadschn.gitlab.io/2014/11/should-you-use-stdstring-stdu16string-or-stdu32string/)
