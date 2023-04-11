#include "renderobject.h"

#include "stb_image.h"

void renderobject::updateMatrices(arp::Pose pose, double aspectRatio)
{
    glm::mat4 projection = glm::perspective(2.0f, (float)aspectRatio, 0.1f, 100.f);
    glm::mat4 camera = glm::translate(glm::mat4(1), pose.position) * glm::mat4(pose.orientation);
    
    glm::mat4 m4( 1.0f );
    m4[ 3 ] = glm::vec4( glm::vec3( xPos, yPos, -translateZ ), 1.0f );
    
    
    glm::mat4 view = glm::inverse(camera) * m4;
    

    glm::mat4 mvNorms = glm::inverse(view);
    mvNorms = glm::transpose(mvNorms);

    glm::mat4 mvp = projection * view;

    prog.SetUniformMatrix4("mvp", &mvp[0][0]);
    prog.SetUniformMatrix4("mv", &view[0][0]);
    prog.SetUniformMatrix3("mvNorms", &mvNorms[0][0]);
   
    
    cy::Matrix3f rotMatrixY = cy::Matrix3f::RotationY(yRot);
    cy::Matrix3f rotMatrixX = cy::Matrix3f::RotationX(xRot);
    cy::Matrix3f rotMatrix = rotMatrixY * rotMatrixX;

    cy::Matrix4f translationMatrix = cy::Matrix4f::Identity();
    translationMatrix.SetColumn(3, xPos, yPos, -translateZ, 1);

    cy::Matrix4f projMatrix = cy::Matrix4f::Perspective(2, (float)aspectRatio, 0.1f, 200.0f);
    cy::Matrix4f view2 = cy::Matrix4f::View(cy::Vec3f(0,0,-25), cy::Vec3f(0,0,0), cy::Vec3f(0,1,0));
    cy::Matrix4f mv = view2 * translationMatrix;

    cy::Matrix3f mvNorms2 = mv.GetSubMatrix3();
    mvNorms2.Invert();
    mvNorms2.Transpose();

   // cy::Matrix4f mvp2 = projMatrix * mv;

    //prog.SetUniformMatrix4("mvp", mvp.cell);
    prog.SetUniformMatrix4("mv", mv.cell);
    prog.SetUniformMatrix3("mvNorms", mvNorms2.cell);
}

void renderobject::render()
{
    glBindBuffer( GL_ARRAY_BUFFER, buffer);
    glEnableVertexAttribArray( pos );
    glVertexAttribPointer(pos, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*) 0);
    glBindBuffer( GL_ARRAY_BUFFER, normalBuffer);
    glEnableVertexAttribArray( norm );
    glVertexAttribPointer(norm, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*) 0);
    glBindBuffer( GL_ARRAY_BUFFER, txcBuffer);
    glEnableVertexAttribArray( txc );
    glVertexAttribPointer(txc, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*) 0);
    
    glUseProgram(prog.GetID());
    glDrawArrays(GL_TRIANGLES, 0, mesh.NF() * 3);
}

renderobject::renderobject(char *fileName, double startingX, double startingY, double startingZ)
{
    xPos = startingX;
    yPos = startingY;
    translateZ = startingZ;

    bool success = mesh.LoadFromFileObj(fileName);
    if(!success)
        return;

    std::vector<cy::Vec3f> vertexData;
    std::vector<cy::Vec3f> normalData;
    std::vector<cy::Vec3f> textureData;
    for(int i = 0; i < mesh.NF(); i++) {
        cy::TriMesh::TriFace vertexFace = mesh.F(i);
        cy::TriMesh::TriFace normalFace = mesh.FN(i);
        cy::TriMesh::TriFace textureFace = mesh.FT(i);
        for(int j = 0; j < 3; j++) {
            vertexData.push_back(mesh.V(vertexFace.v[j]));
            normalData.push_back(mesh.VN(normalFace.v[j]));
            textureData.push_back(mesh.VT(textureFace.v[j]));
        }
    }

    /* Load and decode the texture data*/
    cy::TriMesh::Mtl mtl = mesh.M(0);
    int width, height, numChannels;

    //decode
    const char* filename = mtl.map_Kd;
    unsigned char* imageData = stbi_load(filename, &width, &height, &numChannels, 0);

    //if there's an error, display it
    if(!imageData) {
        std::cout << "decoder error: " << stbi_failure_reason() << std::endl;
    }

    /* Create a vertex array object from the .obj data */
    glGenVertexArrays( 1, &vao);
    glBindVertexArray( vao );

    /* Compile the shaders */
    prog.BuildFiles( "shader4.vert", "shader4.frag" );
    prog.Bind();

    /* Setup the texture */
    cyGLTexture2D cyTex;
    cyTex.Initialize();
    cyTex.SetImage( imageData, numChannels, width, height );
    cyTex.BuildMipmaps();
    cyTex.Bind(0);
    prog["tex"] = 0;
    stbi_image_free(imageData);

    /* Create a vertex buffer object from the .obj data */
    glGenBuffers( 1, &buffer);
    glBindBuffer( GL_ARRAY_BUFFER, buffer);
    glBufferData( GL_ARRAY_BUFFER, sizeof(cy::Vec3f) * vertexData.size(), vertexData.data(), GL_STATIC_DRAW);

    /* Connect the buffer to the vertex shader */
    pos = glGetAttribLocation( prog.GetID(), "pos" );
    glEnableVertexAttribArray( pos );
    glVertexAttribPointer(
         pos, 3, GL_FLOAT,
         GL_FALSE, 0, (GLvoid*) 0);

    /* Create a normal Buffer */
    glGenBuffers( 1, &normalBuffer);
    glBindBuffer( GL_ARRAY_BUFFER, normalBuffer);
    glBufferData( GL_ARRAY_BUFFER, sizeof(cy::Vec3f) * normalData.size(), normalData.data(), GL_STATIC_DRAW);

    /* Connect the normal buffer to the vertex shader */
    norm = glGetAttribLocation( prog.GetID(), "norm" );
    glEnableVertexAttribArray( norm );
    glVertexAttribPointer(
         norm, 3, GL_FLOAT,
         GL_FALSE, 0, (GLvoid*) 0);

    /* Create a texture Buffer */
    glGenBuffers( 1, &txcBuffer);
    glBindBuffer( GL_ARRAY_BUFFER, txcBuffer);
    glBufferData( GL_ARRAY_BUFFER, sizeof(cy::Vec3f) * textureData.size(), textureData.data(), GL_STATIC_DRAW);

    /* Connect the texture buffer to the vertex shader */
    txc = glGetAttribLocation( prog.GetID(), "txc" );
    glEnableVertexAttribArray( txc );
    glVertexAttribPointer(
         txc, 3, GL_FLOAT,
         GL_FALSE, 0, (GLvoid*) 0);
    return;
}
