Synopsis
========

A vector graphics library.


Bugs
====

- Document responsibilities of subclasses
- Platform-specifics
  - SDL suppressing TextInput
  - Clipboard support

- Canvas
  - Stroke path with miter
  - Embed path in Canvas
  - Revisit allocating edge buffer during fill
  - Screen-clip curves
  - Fill with gradient
  - Fill with image
  - Gamma correction (through blit function)
- Fonts
  - Font Indexes
  - Font listing and selection
  - CFF/Postscript outlines
  - Family name
  - Compound glyphs
  - OpenType features
  - Sanitise/Check data? not always easy or possible.
  - Extra properties
- Boxes
  - TextBox
    - Scrolling
    - Selecting
    - Clipboard

- Optmisations
  - Vectorise fill
  - Use truncf() instead of floorf() [re-evaluate -ffast-math]
