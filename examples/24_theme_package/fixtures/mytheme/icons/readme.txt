This is a raster resource (not a real image - a few bytes) - examples/24_theme_package's
own security demo reads it through dg::ThemePackage::read_resource() and decodes it via
dg::ImageCatalog to prove the resource-loading primitive works for a legitimate path, in
the same run that proves a traversal attempt against the same package is rejected.
