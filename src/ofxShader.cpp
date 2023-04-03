#include "ofxShader.h"

ofxShader::ofxShader() {
  m_bWatchingFiles = false;

  // TIME UNIFORMS
  //
  m_uniformsFunctions["u_time"] = UniformFunction([](ofShader* _shader) {
    _shader->setUniform1f("u_time", ofGetElapsedTimef());
  });

  m_uniformsFunctions["u_delta"] = UniformFunction([this](ofShader* _shader) {
    double now = ofGetElapsedTimef();
    _shader->setUniform1f("u_delta", now - m_lastFrame);
    m_lastFrame = now;
  });

  m_uniformsFunctions["u_date"] = UniformFunction([](ofShader* _shader) {
    _shader->setUniform4f("u_date", ofGetYear(), ofGetMonth(), ofGetDay(), ofGetSeconds());
  });

  // MOUSE
  m_uniformsFunctions["u_mouse"] = UniformFunction([](ofShader* _shader) {
    _shader->setUniform2f("u_mouse", ofGetMouseX(), ofGetMouseY());
  });

  // VIEWPORT
  m_uniformsFunctions["u_resolution"] = UniformFunction([](ofShader* _shader) {
    _shader->setUniform2f("u_resolution", ofGetWidth(), ofGetHeight());
  });
}

ofxShader::~ofxShader() {
  disableWatchFiles();
}

string getAbsPath(const string& _str) {
  // string abs_path = realpath(_str.c_str(), NULL);
  string      abs_path = ofFilePath::getAbsolutePath(_str);
  std::size_t found    = abs_path.find_last_of("\\/");
  if (found) {
    return abs_path.substr(0, found);
  } else {
    return "";
  }
}

string urlResolve(const string& _path, const string& _pwd, const std::vector<string> _includeFolders) {
  string url = _pwd + '/' + _path;
  if (ofFile(url).exists()) {
    return url;
  } else {
    for (unsigned int i = 0; i < _includeFolders.size(); i++) {
      string new_path = _includeFolders[i] + "/" + _path;
      if (ofFile(new_path).exists()) {
        return new_path;
      }
    }
    return _path;
  }
}

bool loadFromPath(const string& _path, string* _into, const std::vector<string> _includeFolders) {
  std::ifstream file;
  string        buffer;

  file.open(_path.c_str());
  if (!file.is_open())
    return false;
  string original_path = getAbsPath(_path);

  while (!file.eof()) {
    getline(file, buffer);
    if (buffer.find("#include ") == 0 || buffer.find("#pragma include ") == 0) {
      unsigned begin = buffer.find_first_of("\"");
      unsigned end   = buffer.find_last_of("\"");
      if (begin != end) {
        string file_name = buffer.substr(begin + 1, end - begin - 1);
        file_name        = urlResolve(file_name, original_path, _includeFolders);
        string newBuffer;
        if (loadFromPath(file_name, &newBuffer, _includeFolders)) {
          (*_into) += "\n" + newBuffer + "\n";
        } else {
          std::cout << file_name << " not found at " << original_path << std::endl;
        }
      }
    } else {
      (*_into) += buffer + "\n";
    }
  }

  file.close();
  return true;
}

bool _find_id(const string& program, const char* id) {
  return std::strstr(program.c_str(), id) != 0;
}

bool ofxShader::load(const string& _shaderName) {
  return load(_shaderName + ".vert", _shaderName + ".frag", _shaderName + ".geom");
}

bool ofxShader::load(const string& _vertName, const string& _fragName, const string& _geomName) {
  unload();

  ofShader::setGeometryOutputCount(m_geometryOutputCount);
  ofShader::setGeometryInputType(m_geometryInputType);
  ofShader::setGeometryOutputType(m_geometryOutputType);

  // hackety hack, clear errors or shader will fail to compile
  // GLuint err = glGetError();

  m_lastTimeCheckMillis = ofGetElapsedTimeMillis();
  setMillisBetweenFileCheck(2 * 1000);
  enableWatchFiles();

  m_loadShaderNextFrame = false;

  // Update filenames
  m_vertexShaderFilename   = _vertName;
  m_fragmentShaderFilename = _fragName;
  m_geometryShaderFilename = _geomName;

  // Update last change time
  m_vertexShaderFile.clear();
  m_fragmentShaderFile.clear();
  m_geometryShaderFile.clear();

  m_vertexShaderFile   = ofFile(ofToDataPath(m_vertexShaderFilename));
  m_fragmentShaderFile = ofFile(ofToDataPath(m_fragmentShaderFilename));
  m_geometryShaderFile = ofFile(ofToDataPath(m_geometryShaderFilename));

  m_fileChangedTimes.clear();
  m_fileChangedTimes.push_back(_getLastModified(m_vertexShaderFile));
  m_fileChangedTimes.push_back(_getLastModified(m_fragmentShaderFile));
  m_fileChangedTimes.push_back(_getLastModified(m_geometryShaderFile));

  // Update Sources
  string vertexSrc   = "";
  string fragmentSrc = "";
  string geometrySrc = "";

  // 1. Load shaders resolving #include to nested sources
  loadFromPath(ofToDataPath(m_vertexShaderFilename), &vertexSrc, m_includeFolders);
  loadFromPath(ofToDataPath(m_fragmentShaderFilename), &fragmentSrc, m_includeFolders);
#ifndef TARGET_OPENGLES
  loadFromPath(ofToDataPath(m_geometryShaderFilename), &geometrySrc, m_includeFolders);
#endif

  if (vertexSrc.size() == 0) {
    vertexSrc =
        "\n\
uniform mat4    modelViewProjectionMatrix;\n\
\n\
attribute vec4  position;\n\
attribute vec4  color;\n\
attribute vec2  texcoord;\n\
\n\
varying vec4    v_position;\n\
varying vec4    v_color;\n\
varying vec2    v_texcoord;\n\
\n\
void main() {\n\
    v_position  = position;\n\
    v_color = color;\n\
    v_texcoord  = texcoord;\n\
    gl_Position = modelViewProjectionMatrix * v_position;\n\
}\n";
  }

  // 2. Check active default uniforms
  for (UniformFunctionsList::iterator it = m_uniformsFunctions.begin(); it != m_uniformsFunctions.end(); ++it) {
    it->second.present = (_find_id(vertexSrc, it->first.c_str()) != 0 ||
                          _find_id(fragmentSrc, it->first.c_str()) != 0 ||
                          _find_id(geometrySrc, it->first.c_str()) != 0);
  }

  if (vertexSrc.size() > 0) {
    setupShaderFromSource(GL_VERTEX_SHADER, vertexSrc);
  }

  if (fragmentSrc.size() > 0) {
    setupShaderFromSource(GL_FRAGMENT_SHADER, fragmentSrc);
  }

#ifndef TARGET_OPENGLES
  if (geometrySrc.size() > 0) {
    setupShaderFromSource(GL_GEOMETRY_SHADER_EXT, geometrySrc);
  }
#endif

  bindDefaults();

  bool link = linkProgram();
  ofNotifyEvent(onLoad, link);  //, this);
  return link;
  ;
}

std::string ofxShader::getFilename(GLenum _type) {
  switch (_type) {
    case GL_FRAGMENT_SHADER:
      return m_fragmentShaderFilename;
      break;
    case GL_VERTEX_SHADER:
      return m_vertexShaderFilename;
      break;
#ifndef TARGET_OPENGLES
    case GL_GEOMETRY_SHADER_EXT:
      return m_geometryShaderFilename;
      break;
#endif
    default:
      return "";
      break;
  }
}

void ofxShader::begin() {
  ofShader::begin();

  for (UniformFunctionsList::iterator it = m_uniformsFunctions.begin(); it != m_uniformsFunctions.end(); ++it) {
    if (it->second.present) {
      if (it->second.assign) {
        it->second.assign((ofShader*)this);
      }
    }
  }
}

void ofxShader::_update(ofEventArgs& e) {
  if (m_loadShaderNextFrame) {
    reloadShaders();
    m_loadShaderNextFrame = false;
  }

  int currTime = ofGetElapsedTimeMillis();

  if (((currTime - m_lastTimeCheckMillis) > m_millisBetweenFileCheck) &&
      !m_loadShaderNextFrame) {
    if (_filesChanged()) {
      m_loadShaderNextFrame = true;
      ofNotifyEvent(onChange, m_loadShaderNextFrame, this);
    }

    m_lastTimeCheckMillis = currTime;
  }
}

bool ofxShader::reloadShaders() {
  return load(m_vertexShaderFilename, m_fragmentShaderFilename, m_geometryShaderFilename);
}

void ofxShader::enableWatchFiles() {
  if (!m_bWatchingFiles) {
    ofAddListener(ofEvents().update, this, &ofxShader::_update);
    m_bWatchingFiles = true;
  }
}

void ofxShader::disableWatchFiles() {
  if (m_bWatchingFiles) {
    ofRemoveListener(ofEvents().update, this, &ofxShader::_update);
    m_bWatchingFiles = false;
  }
}

bool ofxShader::_filesChanged() {
  bool fileChanged = false;

  if (m_vertexShaderFile.exists()) {
    std::time_t vertexShaderFileLastChangeTime = _getLastModified(m_vertexShaderFile);
    if (vertexShaderFileLastChangeTime != m_fileChangedTimes.at(0)) {
      m_fileChangedTimes.at(0) = vertexShaderFileLastChangeTime;
      fileChanged              = true;
    }
  }

  if (m_fragmentShaderFile.exists()) {
    std::time_t fragmentShaderFileLastChangeTime = _getLastModified(m_fragmentShaderFile);
    if (fragmentShaderFileLastChangeTime != m_fileChangedTimes.at(1)) {
      m_fileChangedTimes.at(1) = fragmentShaderFileLastChangeTime;
      fileChanged              = true;
    }
  }

  if (m_geometryShaderFile.exists()) {
    std::time_t geometryShaderFileLastChangeTime = _getLastModified(m_geometryShaderFile);
    if (geometryShaderFileLastChangeTime != m_fileChangedTimes.at(2)) {
      m_fileChangedTimes.at(2) = geometryShaderFileLastChangeTime;
      fileChanged              = true;
    }
  }

  return fileChanged;
}

std::time_t ofxShader::_getLastModified(ofFile& _file) {
  if (_file.exists()) {
    return std::filesystem::last_write_time(_file.path());
  } else {
    return 0;
  }
}

void ofxShader::setMillisBetweenFileCheck(int _millis) {
  m_millisBetweenFileCheck = _millis;
}

void ofxShader::setGeometryInputType(GLenum _type) {
  ofShader::setGeometryInputType(_type);
  m_geometryInputType = _type;
}

void ofxShader::setGeometryOutputType(GLenum _type) {
  ofShader::setGeometryOutputType(_type);
  m_geometryOutputType = _type;
}

void ofxShader::setGeometryOutputCount(int _count) {
  ofShader::setGeometryOutputCount(_count);
  m_geometryOutputCount = _count;
}

// void ofxShader::setUniformTextureCube(const string & _name, const ofxTextureCube& _cubemap, int _textureLocation) const {
//     glActiveTexture(GL_TEXTURE0 + _textureLocation);
//     glBindTexture(GL_TEXTURE_CUBE_MAP, _cubemap.getId());

//     setUniform1i(_name, _textureLocation);
//     glActiveTexture(GL_TEXTURE0);
// }