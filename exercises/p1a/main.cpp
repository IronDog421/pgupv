#include <PGUPV.h>

using namespace PGUPV;

/* 
Rellena las funciones setup y render tal y como se explica en el enunciado de la práctica.
¡Cuidado! NO uses las llamadas de OpenGL directamente (glGenVertexArrays, glBindBuffer, etc.).
Usa las clases Model y Mesh de PGUPV.
*/

class MyRender : public Renderer {
public:
	void setup(void) override;
	void render(void) override;
	void reshape(uint w, uint h) override;
private:
	std::shared_ptr<Model> model;
};

void MyRender::setup() {
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

	auto circle_mesh = std::make_shared<Mesh>();
	auto mesh = std::make_shared<Mesh>();
	std::vector<glm::vec3> circle_vertices;
	for (int i = 0; i < 60; i++) {
		float angle = glm::radians(i * 360.0f / 60.0f);
		circle_vertices.push_back({ 0.7f * cos(angle), 0.7f * sin(angle), 0.0f });
	}
	circle_mesh->addVertices(circle_vertices);
	mesh->addVertices({
		{ -0.7f, -0.7f, 0.0f }, // 0, esquina inferior izquierdo
		{ 0.7f, -0.7f, 0.0f },  // 1, esquina inferior derecho
		{ 0.7f, 0.7f, 0.0f },   // 2, esquina superior derecho
		{ -0.7f, 0.7f, 0.0f },  // 3, esquina superior izquierdo
		{ 0.0f, 0.0f, 0.0f },   // 4, centro
		{-0.7f, 0.0f, 0.0f },   // 5, centro izquierda
		{0.0f, -0.7f, 0.0f },   // 6, centro inferior
		{ 0.0f, 0.7f, 0.0f },   // 7, centro superior
		{ 0.7f, 0.0f, 0.0f },   // 8, centro derecho
	});
	mesh->addIndices(
		std::vector<unsigned int>{
		0, 1, 1, 2, 2, 3, 3, 0,
		6, 7, 5, 8
	});
	mesh->setColor(glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f });
	mesh->addDrawCommand(new DrawElements(GL_LINES, 12, GL_UNSIGNED_INT, 0));
	circle_mesh->setColor(glm::vec4{ 1.0f, 0.0f, 0.0f, 1.0f });
	circle_mesh->addDrawCommand(new DrawArrays(GL_LINE_LOOP, 0, 60));

	model = std::make_shared<Model>();
	model->addMesh(circle_mesh);
	model->addMesh(mesh);


	ConstantIllumProgramMVP::use();
}

void MyRender::render() {
	glClear(GL_COLOR_BUFFER_BIT);
	model->render();
}

void MyRender::reshape(uint w, uint h) {
	glViewport(0, 0, w, h);
	/*
	Sistema de coordenadas canónico: [-1, 1]x[-1, 1]x[-1, 1]
	*/
	ConstantIllumProgramMVP::setMVP(glm::mat4{ 1.0f });
}

int main(int argc, char *argv[]) {
	App &myApp = App::getInstance();
	myApp.setInitWindowSize(800, 800);
	myApp.initApp(argc, argv, PGUPV::DOUBLE_BUFFER);
	myApp.getWindow().setRenderer(std::make_shared<MyRender>());
	return myApp.run();
}