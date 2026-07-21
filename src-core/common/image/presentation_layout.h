#pragma once

#include "presentation.h"

#include <string>

namespace image
{
    namespace presentation
    {
        enum class LayoutKind
        {
            Minimal,
            Editorial
        };

        enum class FrameKind
        {
            Portrait,
            Landscape,
            Square
        };

        enum class RasterTransform
        {
            None,
            FlipVertical,
            FlipHorizontal,
            Rotate180
        };

        struct OrientationInfo
        {
            FrameKind frame = FrameKind::Square;
            RasterTransform transform = RasterTransform::None;
            bool north_up_requested = true;
            bool north_up_verified = false;
            bool latitudes_valid = false;
            bool inferred_from_projection = false;
            bool inferred_from_gcps = false;
            bool inferred_from_pass_direction = false;
            double top_latitude = 0.0;
            double bottom_latitude = 0.0;
            std::string pass_direction;
            std::string description;
        };

        FrameKind classify_frame(const Image &source);
        std::string frame_kind_name(FrameKind frame);
        std::string raster_transform_name(RasterTransform transform);

        Image apply_transform(const Image &source, RasterTransform transform);
        Image render_minimal(const Image &source, TextDrawer &text_drawer, const PresentationSpec &spec);
        Image render_layout(const Image &source, TextDrawer &text_drawer, const PresentationSpec &spec, LayoutKind layout);
    }
}
