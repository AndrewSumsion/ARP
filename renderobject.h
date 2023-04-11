#ifndef renderobject_h
#define renderobject_h

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "cyTriMesh.h"
#include "cyGL.h"
#include "cyMatrix.h"
#include "cyVector.h"
#include "glm/glm.hpp"
#include "glm/ext.hpp"
#include "arp.h"

class renderobject
{
private:
    double yRot = 0;
    double xRot = 0;
    double xPos = 0;
    double yPos = 0;
    double translateZ = 0;
    
    const int SCALE_FACTOR = 150;
    
    cy::GLSLProgram prog;
    GLuint buffer;
    GLuint vao;
    GLuint pos;
    GLuint normalBuffer;
    GLuint norm;
    GLuint txc;
    GLuint txcBuffer;
    cy::TriMesh mesh;
    
public:
    void updateMatrices(arp::Pose pose, double aspectRatio, double fovY);
    void render();
    renderobject(char *fileName, double startingX, double startingY, double startingZ);
    
};

#endif /* renderobject_h */
