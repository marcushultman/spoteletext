#include "ttxpagestream.h"

TTXPageStream::TTXPageStream() :
     _isCarousel(false),
     _transitionTime(0),
		 _CarouselPage(this)
{
    //ctor
}

TTXPageStream::TTXPageStream(std::string filename) :
     TTXPage(filename),
     _isCarousel(false),
     _transitionTime(0),
		 _CarouselPage(this)
{
  struct stat attrib;               // create a file attribute structure
  stat(filename.c_str(), &attrib);  // get the attributes of the file
	_modifiedTime=attrib.st_mtime;
}

TTXPageStream::~TTXPageStream()
{
    //dtor
}


TTXLine* TTXPageStream::GetTxRow(uint8_t row)
{
  // Return a line OR NULL if the row does not exist
  TTXLine* line=NULL;
  if (IsCarousel())
  {
    line=_CarouselPage->GetRow(row);
  }
  else // single page
  {
    line=GetRow(row); // _lineCounter is implied
  }
  if (line!=NULL) // Found a line
  {
    // std::cerr << " (R" << (int)row << ") ";
    return line;
  }
  // No more lines? return NULL.
  // std::cerr << " GetNextRow " << _lineCounter << std::endl;
  return NULL;
}

void TTXPageStream::StepNextSubpage()
{
	if (_CarouselPage==NULL) _CarouselPage=this;
	_CarouselPage=_CarouselPage->Getm_SubPage();
	if (_CarouselPage==NULL) // Last carousel subpage? Loop to beginning
		_CarouselPage=this;
}

void TTXPageStream::printList()
{
  std::cerr << "DUMP TODO" << std::endl;
}

bool TTXPageStream::LoadPage(std::string filename)
{
  bool Loaded=false;
    // std::cerr << "[TTXPage] file constructor loading " << filename<< std::endl;
    //m_Init(); // Careful! We should move inits to the initialisation list and call the default constructor
  m_PageNumber=FIRSTPAGE; // Force to replace the root page rather than add to the carousel
  if (m_LoadTTI(filename))
      Loaded=true;
  return Loaded;
}

