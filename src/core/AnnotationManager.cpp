#include "AnnotationManager.h"
#include "PolygonOverlay.h"
#include "PolygonTriangulator.h"
#include "Minimap.h"
#include "imgui.h"
#include <iostream>
#include <cstring>
#include <cmath>


void AnnotationPolygon::ComputeBoundingBox() {
    if (vertices.empty()) {
        boundingBox = Rect{0, 0, 0, 0};
        return;
    }

    double minX = vertices[0].x, maxX = vertices[0].x;
    double minY = vertices[0].y, maxY = vertices[0].y;

    for (const auto& v : vertices) {
        minX = std::min(minX, v.x);
        maxX = std::max(maxX, v.x);
        minY = std::min(minY, v.y);
        maxY = std::max(maxY, v.y);
    }

    boundingBox = Rect{minX, minY, maxX - minX, maxY - minY};
}

bool AnnotationPolygon::ContainsPoint(const Vec2& point) const {
    if (vertices.size() < 3) return false;

    // Ray casting algorithm: cast a ray from point to infinity
    // and count how many times it crosses the polygon edges
    bool inside = false;
    for (size_t i = 0, j = vertices.size() - 1; i < vertices.size(); j = i++) {
        if (((vertices[i].y > point.y) != (vertices[j].y > point.y)) &&
            (point.x < (vertices[j].x - vertices[i].x) * (point.y - vertices[i].y) /
                       (vertices[j].y - vertices[i].y) + vertices[i].x)) {
            inside = !inside;
        }
    }
    return inside;
}

// ========== ANNOTATION MANAGER METHODS ==========

AnnotationManager::AnnotationManager(SDL_Renderer* renderer)
    : renderer_(renderer)
    , nextAnnotationId_(1)
    , toolActive_(false)
    , showRenameDialog_(false)
    , renamingAnnotationIndex_(-1)
{
    renameBuffer_[0] = '\0';
}

AnnotationManager::~AnnotationManager() {
}

void AnnotationManager::SetToolActive(bool active) {
    toolActive_ = active;
    if (!active) {
        drawingState_.Clear();
    }
}

void AnnotationManager::HandleClick(int x, int y, bool isDoubleClick,
                                    const Viewport& viewport, const Minimap* minimap,
                                    PolygonOverlay* polygonOverlay) {
    if (!toolActive_) return;

    // Skip if click is on minimap
    if (minimap && minimap->Contains(x, y)) return;

    Vec2 screenPos(x, y);
    Vec2 slidePos = viewport.ScreenToSlide(screenPos);

    // Double-click completes polygon
    if (isDoubleClick && drawingState_.isActive &&
        drawingState_.currentVertices.size() >= 3) {
        CompletePolygon(polygonOverlay);
        return;
    }

    // Check if clicking near first vertex (close polygon)
    if (drawingState_.isActive &&
        drawingState_.currentVertices.size() >= 3 &&
        IsNearFirstVertex(screenPos, viewport)) {
        CompletePolygon(polygonOverlay);
        return;
    }

    // Add new vertex
    if (!drawingState_.isActive) {
        drawingState_.isActive = true;
    }
    drawingState_.currentVertices.push_back(slidePos);
}

void AnnotationManager::HandleKeyPress(SDL_Keycode key, PolygonOverlay* polygonOverlay) {
    if (key == SDLK_ESCAPE && drawingState_.isActive) {
        // Cancel current polygon drawing
        drawingState_.Clear();
    }
    else if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && drawingState_.isActive) {
        // Complete current polygon
        CompletePolygon(polygonOverlay);
    }
}

void AnnotationManager::UpdateMousePosition(const Vec2& slidePos) {
    drawingState_.mouseSlidePos = slidePos;
}

void AnnotationManager::CompletePolygon(PolygonOverlay* polygonOverlay) {
    if (!drawingState_.isActive || drawingState_.currentVertices.size() < 3) {
        return;
    }

    // Create new annotation
    AnnotationPolygon annotation(nextAnnotationId_++);
    annotation.vertices = drawingState_.currentVertices;
    annotation.ComputeBoundingBox();

    annotations_.push_back(annotation);

    std::cout << "Created annotation: " << annotation.name
              << " with " << annotation.vertices.size() << " vertices" << std::endl;

    // Clear drawing state but keep tool active
    drawingState_.Clear();
    drawingState_.isActive = false;

    // Compute cell counts if polygon overlay is loaded
    if (polygonOverlay) {
        ComputeCellCounts(annotations_.back(), polygonOverlay);
    }
}

bool AnnotationManager::IsNearFirstVertex(Vec2 screenPos, const Viewport& viewport) const {
    if (drawingState_.currentVertices.empty()) {
        return false;
    }

    Vec2 firstScreenPos = viewport.SlideToScreen(drawingState_.currentVertices[0]);
    double distance = std::sqrt(
        std::pow(screenPos.x - firstScreenPos.x, 2) +
        std::pow(screenPos.y - firstScreenPos.y, 2)
    );

    return distance < 10.0;  // 10 pixel threshold
}

void AnnotationManager::ComputeCellCounts(AnnotationPolygon& annotation,
                                          PolygonOverlay* polygonOverlay) {
    annotation.cellCounts.clear();

    if (!polygonOverlay) return;

    // Get all cell polygons from the overlay
    const auto& cellPolygons = polygonOverlay->GetPolygons();

    // For each cell polygon, check if its centroid is inside the annotation
    for (const auto& cellPolygon : cellPolygons) {
        if (cellPolygon.vertices.empty()) continue;

        // Compute centroid of cell polygon
        Vec2 centroid(0, 0);
        for (const auto& vertex : cellPolygon.vertices) {
            centroid.x += vertex.x;
            centroid.y += vertex.y;
        }
        centroid.x /= cellPolygon.vertices.size();
        centroid.y /= cellPolygon.vertices.size();

        // Check if centroid is inside annotation polygon
        if (annotation.ContainsPoint(centroid)) {
            annotation.cellCounts[cellPolygon.classId]++;
        }
    }

    std::cout << "Computed cell counts for " << annotation.name << ": ";
    for (const auto& [classId, count] : annotation.cellCounts) {
        std::cout << "Class " << classId << ": " << count << " ";
    }
    std::cout << std::endl;
}

// ========== PROGRAMMATIC ANNOTATION API ==========

int AnnotationManager::CreateAnnotation(const std::vector<Vec2>& vertices,
                                        const std::string& name,
                                        PolygonOverlay* polygonOverlay) {
    if (!ValidateVertices(vertices)) {
        std::cerr << "Cannot create annotation: invalid vertices" << std::endl;
        return -1;
    }

    // Create new annotation
    AnnotationPolygon annotation(nextAnnotationId_++);
    annotation.vertices = vertices;

    // Set name (use default if empty)
    if (!name.empty()) {
        annotation.name = name;
    }

    annotation.ComputeBoundingBox();

    // Compute cell counts if polygon overlay provided
    if (polygonOverlay) {
        ComputeCellCounts(annotation, polygonOverlay);
    }

    int newId = annotation.id;
    annotations_.push_back(annotation);

    std::cout << "Created annotation programmatically: " << annotation.name
              << " (ID: " << newId << ") with " << annotation.vertices.size()
              << " vertices" << std::endl;

    return newId;
}

AnnotationPolygon* AnnotationManager::GetAnnotationById(int id) {
    for (auto& annotation : annotations_) {
        if (annotation.id == id) {
            return &annotation;
        }
    }
    return nullptr;
}

const AnnotationPolygon* AnnotationManager::GetAnnotationById(int id) const {
    for (const auto& annotation : annotations_) {
        if (annotation.id == id) {
            return &annotation;
        }
    }
    return nullptr;
}

bool AnnotationManager::DeleteAnnotationById(int id) {
    for (auto it = annotations_.begin(); it != annotations_.end(); ++it) {
        if (it->id == id) {
            std::cout << "Deleting annotation by ID: " << it->name
                     << " (ID: " << id << ")" << std::endl;
            annotations_.erase(it);
            return true;
        }
    }
    return false;
}

AnnotationManager::AnnotationMetrics
AnnotationManager::ComputeMetricsForVertices(const std::vector<Vec2>& vertices,
                                             PolygonOverlay* polygonOverlay) const {
    AnnotationMetrics metrics;

    if (!ValidateVertices(vertices)) {
        std::cerr << "Cannot compute metrics: invalid vertices" << std::endl;
        metrics.boundingBox = Rect{0, 0, 0, 0};
        metrics.area = 0.0;
        metrics.perimeter = 0.0;
        metrics.totalCells = 0;
        return metrics;
    }

    // Create temporary annotation to compute metrics
    AnnotationPolygon tempAnnotation(0);  // Temporary ID
    tempAnnotation.vertices = vertices;
    tempAnnotation.ComputeBoundingBox();

    metrics.boundingBox = tempAnnotation.boundingBox;
    metrics.area = ComputeArea(vertices);
    metrics.perimeter = ComputePerimeter(vertices);

    // Compute cell counts if polygon overlay provided
    if (polygonOverlay) {
        const auto& cellPolygons = polygonOverlay->GetPolygons();

        for (const auto& cellPolygon : cellPolygons) {
            if (cellPolygon.vertices.empty()) continue;

            // Compute centroid of cell polygon
            Vec2 centroid(0, 0);
            for (const auto& vertex : cellPolygon.vertices) {
                centroid.x += vertex.x;
                centroid.y += vertex.y;
            }
            centroid.x /= cellPolygon.vertices.size();
            centroid.y /= cellPolygon.vertices.size();

            // Check if centroid is inside the temporary annotation
            if (tempAnnotation.ContainsPoint(centroid)) {
                metrics.cellCounts[cellPolygon.classId]++;
            }
        }
    }

    // Compute total cells
    metrics.totalCells = 0;
    for (const auto& [classId, count] : metrics.cellCounts) {
        metrics.totalCells += count;
    }

    return metrics;
}

// ========== GEOMETRY CALCULATION HELPERS ==========

double AnnotationManager::ComputeArea(const std::vector<Vec2>& vertices) {
    if (vertices.size() < 3) return 0.0;

    // Shoelace formula: Area = 0.5 * |Σ(x_i * y_{i+1} - x_{i+1} * y_i)|
    double area = 0.0;
    size_t n = vertices.size();

    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        area += vertices[i].x * vertices[j].y;
        area -= vertices[j].x * vertices[i].y;
    }

    return std::abs(area) / 2.0;
}

double AnnotationManager::ComputePerimeter(const std::vector<Vec2>& vertices) {
    if (vertices.size() < 2) return 0.0;

    double perimeter = 0.0;
    size_t n = vertices.size();

    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        double dx = vertices[j].x - vertices[i].x;
        double dy = vertices[j].y - vertices[i].y;
        perimeter += std::sqrt(dx * dx + dy * dy);
    }

    return perimeter;
}

bool AnnotationManager::ValidateVertices(const std::vector<Vec2>& vertices) {
    if (vertices.size() < 3) {
        return false;
    }

    // Check for NaN or infinity
    for (const auto& v : vertices) {
        if (std::isnan(v.x) || std::isnan(v.y) ||
            std::isinf(v.x) || std::isinf(v.y)) {
            return false;
        }
    }

    return true;
}

// ========== RENDERING METHODS ==========

void AnnotationManager::RenderAnnotations(const Viewport& viewport) {
    if (annotations_.empty()) return;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (const auto& annotation : annotations_) {
        RenderAnnotationPolygon(annotation, viewport);
    }
}

void AnnotationManager::RenderAnnotationPolygon(const AnnotationPolygon& annotation,
                                                 const Viewport& viewport) {
    if (annotation.vertices.size() < 3) return;

    // Render filled polygon
    if (annotation.triangleIndices.empty()) {
        annotation.triangleIndices = PolygonTriangulator::Triangulate(annotation.vertices);
    }

    if (!annotation.triangleIndices.empty()) {
        std::vector<SDL_Vertex> vertices;

        uint8_t alpha = static_cast<uint8_t>(ANNOTATION_OPACITY * 255);

        // Transform vertices to screen space
        for (const auto& vertex : annotation.vertices) {
            Vec2 screenPos = viewport.SlideToScreen(vertex);
            SDL_Vertex sdlVertex;
            sdlVertex.position = {static_cast<float>(screenPos.x),
                                 static_cast<float>(screenPos.y)};
            sdlVertex.color = {ANNOTATION_COLOR.r, ANNOTATION_COLOR.g,
                              ANNOTATION_COLOR.b, alpha};
            sdlVertex.tex_coord = {0.0f, 0.0f};
            vertices.push_back(sdlVertex);
        }

        // Render filled triangles
        SDL_RenderGeometry(renderer_, nullptr,
            vertices.data(), static_cast<int>(vertices.size()),
            annotation.triangleIndices.data(),
            static_cast<int>(annotation.triangleIndices.size()));
    }

    // Render outline
    SDL_SetRenderDrawColor(renderer_, ANNOTATION_OUTLINE_COLOR.r,
                          ANNOTATION_OUTLINE_COLOR.g,
                          ANNOTATION_OUTLINE_COLOR.b, 255);

    for (size_t i = 0; i < annotation.vertices.size(); ++i) {
        Vec2 v1 = viewport.SlideToScreen(annotation.vertices[i]);
        Vec2 v2 = viewport.SlideToScreen(
            annotation.vertices[(i + 1) % annotation.vertices.size()]);
        SDL_RenderDrawLine(renderer_,
            static_cast<int>(v1.x), static_cast<int>(v1.y),
            static_cast<int>(v2.x), static_cast<int>(v2.y));
    }
}

void AnnotationManager::RenderDrawingPreview(const Viewport& viewport) {
    if (drawingState_.currentVertices.empty()) return;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // Render vertices as circles
    SDL_SetRenderDrawColor(renderer_, DRAWING_VERTEX_COLOR.r,
                          DRAWING_VERTEX_COLOR.g,
                          DRAWING_VERTEX_COLOR.b, 255);

    for (const auto& vertex : drawingState_.currentVertices) {
        Vec2 screenPos = viewport.SlideToScreen(vertex);
        int x = static_cast<int>(screenPos.x);
        int y = static_cast<int>(screenPos.y);

        // Draw a small circle (5 pixel radius)
        for (int dy = -5; dy <= 5; ++dy) {
            for (int dx = -5; dx <= 5; ++dx) {
                if (dx*dx + dy*dy <= 25) {  // Circle equation
                    SDL_RenderDrawPoint(renderer_, x + dx, y + dy);
                }
            }
        }
    }

    // Render edges between vertices
    SDL_SetRenderDrawColor(renderer_, DRAWING_EDGE_COLOR.r,
                          DRAWING_EDGE_COLOR.g,
                          DRAWING_EDGE_COLOR.b, 255);

    for (size_t i = 0; i < drawingState_.currentVertices.size() - 1; ++i) {
        Vec2 v1 = viewport.SlideToScreen(drawingState_.currentVertices[i]);
        Vec2 v2 = viewport.SlideToScreen(drawingState_.currentVertices[i + 1]);
        SDL_RenderDrawLine(renderer_,
            static_cast<int>(v1.x), static_cast<int>(v1.y),
            static_cast<int>(v2.x), static_cast<int>(v2.y));
    }

    // Render preview edge from last vertex to mouse
    if (!drawingState_.currentVertices.empty()) {
        Vec2 lastVertex = viewport.SlideToScreen(
            drawingState_.currentVertices.back());
        Vec2 mouseScreen = viewport.SlideToScreen(drawingState_.mouseSlidePos);

        // Draw dashed line
        SDL_SetRenderDrawColor(renderer_, 150, 150, 150, 200);
        SDL_RenderDrawLine(renderer_,
            static_cast<int>(lastVertex.x), static_cast<int>(lastVertex.y),
            static_cast<int>(mouseScreen.x), static_cast<int>(mouseScreen.y));
    }

    // Highlight first vertex if close enough to close
    if (drawingState_.currentVertices.size() >= 3) {
        int mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);
        if (IsNearFirstVertex(Vec2(mouseX, mouseY), viewport)) {
            Vec2 firstScreen = viewport.SlideToScreen(
                drawingState_.currentVertices[0]);
            int x = static_cast<int>(firstScreen.x);
            int y = static_cast<int>(firstScreen.y);

            // Draw larger highlight circle
            SDL_SetRenderDrawColor(renderer_, 255, 255, 0, 200);
            for (int dy = -8; dy <= 8; ++dy) {
                for (int dx = -8; dx <= 8; ++dx) {
                    if (dx*dx + dy*dy <= 64) {
                        SDL_RenderDrawPoint(renderer_, x + dx, y + dy);
                    }
                }
            }
        }
    }
}

void AnnotationManager::RenderUI(PolygonOverlay* polygonOverlay) {
    ImGui::Text("Annotations: %d", GetAnnotationCount());

    if (toolActive_) {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f),
                          "Drawing mode active");
    }

    ImGui::Separator();

    if (annotations_.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                          "No annotations yet");
        ImGui::Text("Use the Polygon Tool to draw annotations");
        return;
    }

    // List annotations
    ImGui::BeginChild("AnnotationList", ImVec2(0, 0), true);

    int deleteIndex = -1;  // Track which to delete outside the loop

    for (int i = 0; i < static_cast<int>(annotations_.size()); ++i) {
        ImGui::PushID(i);

        auto& annotation = annotations_[i];

        // Annotation name (double-click to rename)
        if (ImGui::Selectable(annotation.name.c_str(), false,
                             ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                StartRenaming(i);
            }
        }

        // Delete button
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            deleteIndex = i;
        }

        // Show vertex count
        ImGui::SameLine();
        ImGui::TextDisabled("(%d vertices)",
                           static_cast<int>(annotation.vertices.size()));

        // Show cell counts if available
        if (!annotation.cellCounts.empty()) {
            ImGui::Indent();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Cells inside:");

            int totalCells = 0;
            for (const auto& [classId, count] : annotation.cellCounts) {
                totalCells += count;
                // Get class color from polygon overlay if available
                SDL_Color classColor = {200, 200, 200, 255};  // Default gray
                if (polygonOverlay) {
                    classColor = polygonOverlay->GetClassColor(classId);
                }

                ImGui::BulletText("Class %d: %d", classId, count);
                ImGui::SameLine();
                // Show color indicator
                ImGui::ColorButton("##color", ImVec4(classColor.r / 255.0f,
                                                     classColor.g / 255.0f,
                                                     classColor.b / 255.0f,
                                                     1.0f),
                                  ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                                  ImVec2(12, 12));
            }

            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f), "Total: %d cells", totalCells);
            ImGui::Unindent();
        } else if (polygonOverlay && polygonOverlay->GetPolygonCount() > 0) {
            ImGui::Indent();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No cells inside");
            ImGui::Unindent();
        }

        ImGui::PopID();
    }

    ImGui::EndChild();

    // Delete after the loop to avoid iterator issues
    if (deleteIndex >= 0) {
        DeleteAnnotation(deleteIndex);
    }

    // Rename dialog - open popup when flag is set
    if (showRenameDialog_) {
        ImGui::OpenPopup("Rename Annotation");
        showRenameDialog_ = false;  // Only open once
    }

    // Render rename modal
    if (ImGui::BeginPopupModal("Rename Annotation", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter new name:");

        // Set focus on first frame
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }

        bool submitRename = ImGui::InputText("##rename", renameBuffer_, sizeof(renameBuffer_),
                                             ImGuiInputTextFlags_EnterReturnsTrue);

        if (ImGui::Button("OK", ImVec2(120, 0)) || submitRename) {
            if (renamingAnnotationIndex_ >= 0 &&
                renamingAnnotationIndex_ < static_cast<int>(annotations_.size())) {
                annotations_[renamingAnnotationIndex_].name = renameBuffer_;
            }
            renamingAnnotationIndex_ = -1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            renamingAnnotationIndex_ = -1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void AnnotationManager::DeleteAnnotation(int index) {
    if (index >= 0 && index < static_cast<int>(annotations_.size())) {
        std::cout << "Deleting annotation: " << annotations_[index].name << std::endl;
        annotations_.erase(annotations_.begin() + index);
    }
}

void AnnotationManager::StartRenaming(int index) {
    if (index >= 0 && index < static_cast<int>(annotations_.size())) {
        renamingAnnotationIndex_ = index;
        strncpy(renameBuffer_, annotations_[index].name.c_str(),
                sizeof(renameBuffer_) - 1);
        renameBuffer_[sizeof(renameBuffer_) - 1] = '\0';
        showRenameDialog_ = true;
    }
}
