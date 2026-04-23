#include "PlayPauseButton.h"

void PlayPauseButton::paintButton(
    juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown
) {
  // Circular chrome that mirrors LookAndFeel_V4::drawButtonBackground: fill
  // with buttonColourId, stroke with ComboBox::outlineColourId, and tint on
  // hover/press via Colour::contrasting so the visual matches the adjacent
  // Add / Remove rectangles exactly.
  const auto area = getLocalBounds().toFloat();
  const float diameter = juce::jmin(area.getWidth(), area.getHeight());
  // Inset half a pixel so the 1 px stroke lands on pixel boundaries rather
  // than being split across two rows / columns (which would look fuzzy).
  const auto circle =
      juce::Rectangle<float>(diameter - 1.0f, diameter - 1.0f).withCentre(area.getCentre());

  auto fill =
      findColour(juce::TextButton::buttonColourId).withMultipliedAlpha(isEnabled() ? 1.0f : 0.5f);
  if (shouldDrawButtonAsDown)
    fill = fill.contrasting(0.2f);
  else if (shouldDrawButtonAsHighlighted)
    fill = fill.contrasting(0.05f);

  g.setColour(fill);
  g.fillEllipse(circle);

  g.setColour(findColour(juce::ComboBox::outlineColourId));
  g.drawEllipse(circle, 1.0f);

  // Glyph sized to about 40% of the circle's diameter: big enough to read as
  // an icon at a glance, small enough to leave comfortable empty space inside.
  const float iconH = diameter * 0.4f;
  const float iconW = iconH * 0.9f;

  g.setColour(
      findColour(juce::TextButton::textColourOffId).withMultipliedAlpha(isEnabled() ? 1.0f : 0.5f)
  );

  if (showingPause) {
    const auto iconArea = juce::Rectangle<float>(iconW, iconH).withCentre(area.getCentre());
    const float barW = iconArea.getWidth() * 0.35f;
    g.fillRect(iconArea.withWidth(barW));
    g.fillRect(iconArea.withX(iconArea.getRight() - barW).withWidth(barW));
  } else {
    // The triangle's centroid sits left of the bounding-box centre. Shift the
    // box right so the visual mass lands on the circle centre.
    const auto iconArea = juce::Rectangle<float>(iconW, iconH)
                              .withCentre({area.getCentreX() + (iconW / 6.0f), area.getCentreY()});
    juce::Path triangle;
    triangle.addTriangle(
        iconArea.getTopLeft(),
        iconArea.getBottomLeft(),
        {iconArea.getRight(), iconArea.getCentreY()}
    );
    g.fillPath(triangle);
  }
}
