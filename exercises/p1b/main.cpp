#include <PGUPV.h>
#include <GUI3.h>
#include <iomanip>
#include <algorithm>
#include <limits>
#include "gmlReader.h"
#include "../p2/kmlReader.h"
#include "../p2/kmlReader.cpp"
#include "../p2/UTM.cpp"

using namespace PGUPV;

class MyRender : public Renderer {
public:
    MyRender() : axes(500.0f) {};
    void setup(void) override;
    void render(void) override;
    void reshape(uint w, uint h) override;
    bool mouse_move(const MouseMotionEvent&) override;

private:
    void buildGUI();
    std::shared_ptr<GLMatrices> mats;
    std::unique_ptr<PGUPV::Mesh> boundary;

    std::unique_ptr<PGUPV::Mesh> meshExterior; // single mesh for efficiency
    std::unique_ptr<PGUPV::Mesh> meshInterior;

	std::unique_ptr<PGUPV::Mesh> meshNhOutlines; // single mesh for all neighborhood outlines
	std::vector<std::unique_ptr<PGUPV::Mesh>> meshAreas; // mask meshes for each neighborhood
	std::vector<std::unique_ptr<PGUPV::Mesh>> meshPerimeters; // fill meshes for each neighborhood
    std::shared_ptr<Label> cursorPos;
    glm::uvec2 windowSize{ 0 };
    glm::dvec2 cityOrigin;

	std::vector<std::string> names; // neighborhood names
    int selectedNh = 0;
    glm::vec4 nhColor{ 1.0f, 0.0f, 0.0f, 0.5f };

    Axes axes;
};

/**
 * @brief Aquí se define el panel de control de la aplicación
*/
void MyRender::buildGUI() {
	// Creamos un panel nuevo donde introducir nuestros controles, llamado Configuracion
    auto panel = addPanel("Location");
	// Tamaño del panel y posición del panel
	panel->setSize(260, 100);
	panel->setPosition(530, 10);

    panel->addWidget(std::make_shared<Label>("Cursor pos: "));
    cursorPos = std::make_shared<Label>("");
    panel->addWidget(cursorPos);

    if (!names.empty()) {
        auto list = std::make_shared<ListBoxWidget<>>("Neighborhoods", names, selectedNh);
        list->getValue().addListener([this](int idx) { selectedNh = idx; }); 
        panel->addWidget(list);

        auto colorWidget = std::make_shared<RGBAColorWidget>("Color", nhColor);
        colorWidget->getValue().addListener([this](const glm::vec4 &value) { nhColor = value; });
        panel->addWidget(colorWidget);
    }

    App::getInstance().getWindow().showGUI(true);
}

/**
 * @brief Este método se llama cada vez que el ratón se mueve
 * @param me información sobre el evento de movimiento del ratón
 * @return devolver true si se quiere capturar el evento
*/
bool MyRender::mouse_move(const MouseMotionEvent& me) {
    if (windowSize.x == 0 || windowSize.y == 0) return false;

    auto cam = std::static_pointer_cast<XYPanZoomCamera>(getCameraHandler());
    auto center = cam->getCenter();

    float normX = (me.x / static_cast<float>(windowSize.x)) - 0.5f;
    float normY = 0.5f - (me.y / static_cast<float>(windowSize.y));

    double worldX = center.x + (normX * cam->getWidth());
    double worldY = center.y + (normY * cam->getHeight());

    double utmX = worldX + cityOrigin.x;
    double utmY = worldY + cityOrigin.y;

    std::ostringstream os;
    os << std::fixed << std::setprecision(2) << utmX << " " << utmY;
    cursorPos->setText(os.str());

    return false;
}

void MyRender::setup() {
    glClearColor(0.6f, 0.6f, 0.9f, 1.0f);
    mats = GLMatrices::build();

    auto city = readBuildings(App::assetsDir() + "data_gis/A.ES.SDGC.BU.46900.buildingpart.gml", true);
    cityOrigin = city.min;

    boundary = std::make_unique<PGUPV::Mesh>();
    boundary->addVertices({
        { 0.0f, 0.0f },
        { static_cast<float>(city.max.x - city.min.x), 0.0f },
        { static_cast<float>(city.max.x - city.min.x), static_cast<float>(city.max.y - city.min.y) },
        { 0.0f, static_cast<float>(city.max.y - city.min.y) }
        });
    boundary->addDrawCommand(new PGUPV::DrawArrays(GL_LINE_LOOP, 0, 4));

    meshExterior = std::make_unique<PGUPV::Mesh>();
    meshInterior = std::make_unique<PGUPV::Mesh>();

    std::vector<glm::vec2> extVerts;
    std::vector<glm::vec2> intVerts;

    for (const auto& building : city.buildings) {
        for (const auto& part : building.parts) {
            GLint firstExt = static_cast<GLint>(extVerts.size());
            for (const auto& pt : part.exterior) {
                extVerts.push_back(glm::vec2(pt - cityOrigin));
            }
            meshExterior->addDrawCommand(new PGUPV::DrawArrays(GL_LINE_LOOP, firstExt, static_cast<GLsizei>(extVerts.size() - firstExt)));

            for (const auto& interior : part.interior) {
                GLint firstInt = static_cast<GLint>(intVerts.size());
                for (const auto& pt : interior) {
                    intVerts.push_back(glm::vec2(pt - cityOrigin));
                }
                meshInterior->addDrawCommand(new PGUPV::DrawArrays(GL_LINE_LOOP, firstInt, static_cast<GLsizei>(intVerts.size() - firstInt)));
            }
        }
    }

    meshExterior->addVertices(extVerts);
    meshInterior->addVertices(intVerts);

    auto neighborhoods = readNeighborhood(App::assetsDir() + "data_gis/barris-barrios.kml");

    names.clear();
    meshNhOutlines = std::make_unique<PGUPV::Mesh>();
    std::vector<glm::vec2> nhOutlineVerts;

    for (const auto& nh : neighborhoods.placemarks) {
        std::string name = nh.id;
        auto attrIt = nh.attributes.find("nombre");
        if (attrIt != nh.attributes.end() && std::holds_alternative<std::string>(attrIt->second)) {
            name = std::get<std::string>(attrIt->second);
        }
        names.push_back(name);

        auto fanMesh = std::make_unique<PGUPV::Mesh>();
        auto bboxMesh = std::make_unique<PGUPV::Mesh>();
        std::vector<glm::vec2> localVerts;
        std::vector<glm::vec2> fanVerts;
        bool fanCaptured = false;

        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = -std::numeric_limits<float>::max();
        float maxY = -std::numeric_limits<float>::max();

        for (const auto& poly : nh.geometry) {
            if (poly.outerBoundary.empty())
                continue;

            GLint firstOutline = static_cast<GLint>(nhOutlineVerts.size());
            for (const auto& pt : poly.outerBoundary) {
                glm::dvec2 diff = pt - cityOrigin;
                glm::vec2 v = glm::vec2(static_cast<float>(diff.x), static_cast<float>(diff.y));
                localVerts.push_back(v);
                nhOutlineVerts.push_back(v);
                if (!fanCaptured)
                    fanVerts.push_back(v);

                minX = std::min(minX, v.x);
                minY = std::min(minY, v.y);
                maxX = std::max(maxX, v.x);
                maxY = std::max(maxY, v.y);
            }

            fanCaptured = true;
            meshNhOutlines->addDrawCommand(new PGUPV::DrawArrays(GL_LINE_LOOP, firstOutline, static_cast<GLsizei>(poly.outerBoundary.size())));
        }

        if (!fanVerts.empty()) {
            fanMesh->addVertices(fanVerts);
            fanMesh->addDrawCommand(new PGUPV::DrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(fanVerts.size())));
        }

        if (localVerts.empty()) {
            bboxMesh->addVertices({ {0.f, 0.f}, {0.f, 0.f}, {0.f, 0.f}, {0.f, 0.f} });
        }
        else {
            bboxMesh->addVertices({
                { minX, minY },
                { maxX, minY },
                { maxX, maxY },
                { minX, maxY }
                });
        }
        bboxMesh->addDrawCommand(new PGUPV::DrawArrays(GL_TRIANGLE_FAN, 0, 4));

        meshAreas.push_back(std::move(fanMesh));
        meshPerimeters.push_back(std::move(bboxMesh));
    }

    meshNhOutlines->addVertices(nhOutlineVerts);

    buildGUI();

    setCameraHandler(std::make_shared<XYPanZoomCamera>(1000.0f, glm::vec3{ 0.0f }));
}

void MyRender::render() {
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    mats->setMatrix(GLMatrices::PROJ_MATRIX, getCamera().getProjMatrix());
    mats->setMatrix(GLMatrices::VIEW_MATRIX, getCamera().getViewMatrix());

    ConstantUniformColorProgram::use();

    ConstantUniformColorProgram::setColor(glm::vec4{ 0.8f, 0.1f, 0.1f, 1.0f });
    boundary->render();

    ConstantUniformColorProgram::setColor(glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
    meshExterior->render();

    auto cam = std::static_pointer_cast<XYPanZoomCamera>(getCameraHandler());
    if (cam->getWidth() < 500.0f) {
        ConstantUniformColorProgram::setColor(glm::vec4{ 1.0f, 0.0f, 0.0f, 1.0f });
        meshInterior->render();
    }

    ConstantUniformColorProgram::setColor(glm::vec4{ 1.0f, 0.0f, 0.0f, 1.0f });
    meshNhOutlines->render();

    if (selectedNh >= 0 && selectedNh < meshAreas.size()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_STENCIL_TEST);

        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glStencilFunc(GL_ALWAYS, 0, 1);
        glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);

        meshAreas[selectedNh]->render();

        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glStencilFunc(GL_NOTEQUAL, 0, 1);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

        ConstantUniformColorProgram::setColor(nhColor);
        meshPerimeters[selectedNh]->render();

        glDisable(GL_STENCIL_TEST);
        glDisable(GL_BLEND);
    }
}

void MyRender::reshape(uint w, uint h) {
    glViewport(0, 0, w, h);
    windowSize = glm::uvec2{ w, h };
}

int main(int argc, char* argv[]) {
    App& myApp = App::getInstance();
    myApp.initApp(argc, argv, PGUPV::DOUBLE_BUFFER | PGUPV::STENCIL_BUFFER);
    myApp.getWindow().setRenderer(std::make_shared<MyRender>());
    return myApp.run();
}
