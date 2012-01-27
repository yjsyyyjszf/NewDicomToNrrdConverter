#include <iostream>
#include <string>
#include <sstream>
#include "DicomToNrrdConverterCLP.h"
#include "itkMacro.h"
#include "itkGDCMSeriesFileNames.h"
#include "itkGDCMImageIO.h"
#include "itkRawImageIO.h"
#include "itkImage.h"
#include "itkImageRegionIteratorWithIndex.h"
#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"
#include "itkImageSeriesReader.h"
#include "itksys/Directory.hxx"
#include "itksys/SystemTools.hxx"
#include "itksys/Base64.h"
#undef HAVE_SSTREAM   // 'twould be nice if people coded without using
                                // incredibly generic macro names
#include "osconfig.h" // make sure OS specific configuration is included first

#define INCLUDE_CSTDIO
#define INCLUDE_CSTRING
#include "ofstdinc.h"
#include "dcvrds.h"
#include "dcdict.h"             // For DcmDataDictionary
#include "dctk.h"          /* for various dcmdata headers */
#include "cmdlnarg.h"      /* for prepareCmdLineArgs */
#include "dcuid.h"         /* for dcmtk version name */
#include "dcrledrg.h"      /* for DcmRLEDecoderRegistration */

#include "dcmimage.h"     /* for DicomImage */
#include "digsdfn.h"      /* for DiGSDFunction */
#include "diciefn.h"      /* for DiCIELABFunction */

#include "ofconapp.h"        /* for OFConsoleApplication */
#include "ofcmdln.h"         /* for OFCommandLine */

#include "diregist.h"     /* include to support color images */
#include "ofstd.h"           /* for OFStandard */

#define DCMTKException(body)                    \
  {                                             \
  if(throwException)                            \
    {                                           \
    itkGenericExceptionMacro(body);             \
    }                                           \
  else                                          \
    {                                           \
    std::cerr body;                             \
    return EXIT_FAILURE;                        \
    }                                           \
  }

class DCMTKSequence
{
public:
  DCMTKSequence() : m_DcmSequenceOfItems(0) {}
  void SetDcmSequenceOfItems(DcmSequenceOfItems *seq)
    {
      this->m_DcmSequenceOfItems = seq;
    }
  int card() { return this->m_DcmSequenceOfItems->card(); }
  int GetSequence(unsigned long index,
                  DCMTKSequence &target,bool throwException = true)
    {
      DcmItem *item = this->m_DcmSequenceOfItems->getItem(index);
      DcmSequenceOfItems *sequence =
        dynamic_cast<DcmSequenceOfItems *>(item);
      if(sequence == 0)
        {
        DCMTKException(<< "Can't find DCMTKSequence at index " << index);
        }
      target.SetDcmSequenceOfItems(sequence);
      return EXIT_SUCCESS;
    }
  int GetStack(unsigned short group,
                unsigned short element,
                DcmStack &resultStack, bool throwException = true)
    {
      DcmTagKey tagkey(group,element);
      if(this->m_DcmSequenceOfItems->search(tagkey,resultStack) != EC_Normal)
        {
        DCMTKException(<< "Can't find tag " << std::hex << group << " "
                       << element << std::dec);
        }
      return EXIT_SUCCESS;
    }

  int GetElementCS(unsigned short group,
                    unsigned short element,
                    std::string &target,
                    bool throwException = true)
    {
      DcmStack resultStack;
      this->GetStack(group,element,resultStack);
      DcmCodeString *codeStringElement = dynamic_cast<DcmCodeString *>(resultStack.top());
      if(codeStringElement == 0)
        {
          DCMTKException(<< "Can't get CodeString Element at tag "
                                   << std::hex << group << " "
                                   << element << std::dec);
        }
      OFString ofString;
      if(codeStringElement->getOFStringArray(ofString) != EC_Normal)
          {
          DCMTKException(<< "Can't get OFString Value at tag "
                         << std::hex << group << " "
                         << element << std::dec);
          }
      target = "";
      for(unsigned j = 0; j < ofString.length(); ++j)
        {
        target += ofString[j];
        }
      return EXIT_SUCCESS;
    }
  int GetElementFD(unsigned short group,
                    unsigned short element,
                    double * &target,
                    bool throwException = true)
    {
      DcmStack resultStack;
      this->GetStack(group,element,resultStack);
      DcmFloatingPointDouble *fdItem = dynamic_cast<DcmFloatingPointDouble *>(resultStack.top());
      if(fdItem == 0)
        {
          DCMTKException(<< "Can't get CodeString Element at tag "
                                   << std::hex << group << " "
                                   << element << std::dec);
        }
      if(fdItem->getFloat64Array(target) != EC_Normal)
        {
        DCMTKException(<< "Can't get floatarray Value at tag "
                       << std::hex << group << " "
                       << element << std::dec);
        }
      return EXIT_SUCCESS;
    }
  int GetElementFD(unsigned short group,
                    unsigned short element,
                    double &target,
                    bool throwException = true)
    {
      double *array;
      this->GetElementFD(group,element,array,throwException);
      target = array[0];
      return EXIT_SUCCESS;
    }
  int GetElementDS(unsigned short group,
                  unsigned short element,
                  std::string &target,
                  bool throwException = true)
    {
      DcmStack resultStack;
      this->GetStack(group,element,resultStack);
      DcmDecimalString *decimalStringElement = dynamic_cast<DcmDecimalString *>(resultStack.top());
      if(decimalStringElement == 0)
        {
        DCMTKException(<< "Can't get DecimalString Element at tag "
                       << std::hex << group << " "
                       << element << std::dec);
        }
      // check for # of expected numbers in DS
      std::stringstream ss;
      ss << target;
      OFString arity(ss.str().c_str());
      if(decimalStringElement->checkValue(arity) != EC_Normal)
        {
        DCMTKException(<< "Value doesn't have proper number of elements");
        }
      OFString ofString;
      if(decimalStringElement->getOFStringArray(ofString) != EC_Normal)
        {
        DCMTKException(<< "Can't get DecimalString Value at tag "
                       << std::hex << group << " "
                       << element << std::dec);
        }
      target = "";
      for(unsigned j = 0; j < ofString.length(); ++j)
        {
        target += ofString[j];
        }
      return EXIT_SUCCESS;
    }
  int GetElementSQ(unsigned short group,
                  unsigned short element,
                  DCMTKSequence &target,
                  bool throwException = true)
    {
      DcmTagKey tagkey(group,element);
      DcmStack resultStack;
      this->GetStack(group,element,resultStack);

      DcmSequenceOfItems *seqElement = dynamic_cast<DcmSequenceOfItems *>(resultStack.top());
      if(seqElement == 0)
        {
          DCMTKException(<< "Can't get  at tag "
                                   << std::hex << group << " "
                                   << element << std::dec);
        }
      target.SetDcmSequenceOfItems(seqElement);
      return EXIT_SUCCESS;
    }

private:
  DcmSequenceOfItems *m_DcmSequenceOfItems;
};

class DCMTKFileReader
{
public:
  DCMTKFileReader() : m_DFile(0),
                      m_Dataset(0),
                      m_DicomImage(0),
                      m_Xfer(EXS_Unknown),
                      m_FrameCount(0)
    {
    }
  ~DCMTKFileReader()
    {

delete m_DFile;
      delete m_DicomImage;
    }
  void SetFileName(const std::string &fileName)
    {
      this->m_FileName = fileName;
    }
  void LoadFile()
    {
      if(this->m_FileName == "")
        {
        itkGenericExceptionMacro(<< "No filename given" );
        }
      if(this->m_DFile != 0)
        {
        delete this->m_DFile;
        }
      this->m_DFile = new DcmFileFormat();
      OFCondition cond = this->m_DFile->loadFile(this->m_FileName.c_str());
      if(cond.bad())
        {
        itkGenericExceptionMacro(<< cond.text() << ": reading file " << this->m_FileName);
        }
      this->m_Dataset = this->m_DFile->getDataset();
      this->m_Xfer = this->m_Dataset->getOriginalXfer();
      this->m_DicomImage = new DicomImage(this->m_DFile,this->m_Xfer,CIF_DecompressCompletePixelData,0,0);
      if(this->m_DicomImage  == 0)
        {
        itkGenericExceptionMacro(<< "Allocating DCMTK Image failed" );
        }
      if(this->m_Dataset->findAndGetSint32(DCM_NumberOfFrames,this->m_FrameCount).bad())
        {
        this->m_FrameCount = 1;
        }
    }
  int GetElementLO(unsigned short group,
                  unsigned short element,
                  std::string &target,
                  bool throwException = true)
    {
      DcmTagKey tagkey(group,element);
      DcmElement *el;
      if(this->m_Dataset->findAndGetElement(tagkey,el) != EC_Normal)
        {
          DCMTKException(<< "Cant find tag " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      DcmLongString *loItem = dynamic_cast<DcmLongString *>(el);
      if(loItem == 0)
        {
          DCMTKException(<< "Cant find DecimalString element " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      OFString ofString;
      if(loItem->getOFStringArray(ofString) != EC_Normal)
        {
        DCMTKException(<< "Cant get string from element " << std::hex
                       << group << " " << std::hex
                       << element << std::dec);
        }
      target = "";
      for(unsigned i = 0; i < ofString.size(); i++)
        {
        target += ofString[i];
        }
      return EXIT_SUCCESS;
    }

  int GetElementLO(unsigned short group,
                    unsigned short element,
                    std::vector<std::string> &target,
                    bool throwException = true)
    {
      DcmTagKey tagkey(group,element);
      DcmElement *el;
      if(this->m_Dataset->findAndGetElement(tagkey,el) != EC_Normal)
        {
          DCMTKException(<< "Cant find tag " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      DcmLongString *loItem = dynamic_cast<DcmLongString *>(el);
      if(loItem == 0)
        {
          DCMTKException(<< "Cant find DecimalString element " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      target.clear();
      OFString ofString;
      for(unsigned long i = 0; loItem->getOFString(ofString,i) == EC_Normal; ++i)
        {
        std::string targetStr = "";
        for(unsigned i = 0; i < ofString.size(); i++)
          {
          targetStr += ofString[i];
          }
        target.push_back(targetStr);
        }
      return EXIT_SUCCESS;
    }

   /** Get an array of data values, as contained in a DICOM
    * DecimalString Item
    */
  template <typename TType>
  int  GetElementDS(unsigned short group,
                     unsigned short element,
                     unsigned short count,
                     TType  *target,
                     bool throwException = true)
    {
      DcmTagKey tagkey(group,element);
      DcmElement *el;
      if(this->m_Dataset->findAndGetElement(tagkey,el) != EC_Normal)
        {
          DCMTKException(<< "Cant find tag " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      DcmDecimalString *dsItem = dynamic_cast<DcmDecimalString *>(el);
      if(dsItem == 0)
        {
          DCMTKException(<< "Cant find DecimalString element " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      OFVector<Float64> doubleVals;
      if(dsItem->getFloat64Vector(doubleVals) != EC_Normal)
        {
          DCMTKException(<< "Cant extract Array from DecimalString " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      if(doubleVals.size() != count)
        {
          DCMTKException(<< "DecimalString " << std::hex
                                   << group << " " << std::hex
                                   << element << " expected "
                                   << count << "items, but found "
                                   << doubleVals.size() << std::dec);

        }
      for(unsigned i = 0; i < count; i++)
        {
        target[i] = static_cast<TType>(doubleVals[i]);
        }
      return EXIT_SUCCESS;
    }
  /** Get a DecimalString Item as a single string
   */
  int  GetElementDS(unsigned short group,
                     unsigned short element,
                     std::string &target,
                     bool throwException = true)
    {
      DcmTagKey tagkey(group,element);
      DcmElement *el;
      if(this->m_Dataset->findAndGetElement(tagkey,el) != EC_Normal)
        {
          DCMTKException(<< "Cant find tag " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      DcmDecimalString *dsItem = dynamic_cast<DcmDecimalString *>(el);
      if(dsItem == 0)
        {
          DCMTKException(<< "Cant find DecimalString element " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      OFString ofString;
      if(dsItem->getOFStringArray(ofString) != EC_Normal)
        {
        DCMTKException(<< "Can't get DecimalString Value at tag "
                       << std::hex << group << " "
                       << element << std::dec);
        }
      target = "";
      for(unsigned j = 0; j < ofString.length(); ++j)
        {
        target += ofString[j];
        }
      return EXIT_SUCCESS;
    }

  int  GetElementFD(unsigned short group,
                     unsigned short element,
                     double &target,
                     bool throwException = true)
    {
      DcmTagKey tagkey(group,element);
      DcmElement *el;
      if(this->m_Dataset->findAndGetElement(tagkey,el) != EC_Normal)
        {
          DCMTKException(<< "Cant find tag " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      DcmFloatingPointDouble *fdItem = dynamic_cast<DcmFloatingPointDouble *>(el);
      if(fdItem == 0)
        {
          DCMTKException(<< "Cant find DecimalString element " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      if(fdItem->getFloat64(target) != EC_Normal)
        {
          DCMTKException(<< "Cant extract Array from DecimalString " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      return EXIT_SUCCESS;
    }
  int  GetElementFD(unsigned short group,
                     unsigned short element,
                     double * &target,
                     bool throwException = true)
    {
      DcmTagKey tagkey(group,element);
      DcmElement *el;
      if(this->m_Dataset->findAndGetElement(tagkey,el) != EC_Normal)
        {
          DCMTKException(<< "Cant find tag " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      DcmFloatingPointDouble *fdItem = dynamic_cast<DcmFloatingPointDouble *>(el);
      if(fdItem == 0)
        {
          DCMTKException(<< "Cant find DecimalString element " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      if(fdItem->getFloat64Array(target) != EC_Normal)
        {
          DCMTKException(<< "Cant extract Array from DecimalString " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      return EXIT_SUCCESS;
    }
  int  GetElementFL(unsigned short group,
                     unsigned short element,
                     float &target,
                     bool throwException = true)
    {
      DcmTagKey tagkey(group,element);
      DcmElement *el;
      if(this->m_Dataset->findAndGetElement(tagkey,el) != EC_Normal)
        {
          DCMTKException(<< "Cant find tag " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      DcmFloatingPointSingle *flItem = dynamic_cast<DcmFloatingPointSingle *>(el);
      if(flItem == 0)
        {
          DCMTKException(<< "Cant find DecimalString element " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      if(flItem->getFloat32(target) != EC_Normal)
        {
          DCMTKException(<< "Cant extract Array from DecimalString " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      return EXIT_SUCCESS;
    }
  int  GetElementUS(unsigned short group,
                     unsigned short element,
                     unsigned short &target,
                     bool throwException = true)
    {
      DcmTagKey tagkey(group,element);
      DcmElement *el;
      if(this->m_Dataset->findAndGetElement(tagkey,el) != EC_Normal)
        {
          DCMTKException(<< "Cant find tag " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      DcmUnsignedShort *usItem = dynamic_cast<DcmUnsignedShort *>(el);
      if(usItem == 0)
        {
          DCMTKException(<< "Cant find DecimalString element " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      if(usItem->getUint16(target) != EC_Normal)
        {
          DCMTKException(<< "Cant extract Array from DecimalString " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      return EXIT_SUCCESS;
    }
  int  GetElementUS(unsigned short group,
                     unsigned short element,
                     unsigned short *&target,
                     bool throwException = true)
    {
      DcmTagKey tagkey(group,element);
      DcmElement *el;
      if(this->m_Dataset->findAndGetElement(tagkey,el) != EC_Normal)
        {
          DCMTKException(<< "Cant find tag " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      DcmUnsignedShort *usItem = dynamic_cast<DcmUnsignedShort *>(el);
      if(usItem == 0)
        {
          DCMTKException(<< "Cant find DecimalString element " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      if(usItem->getUint16Array(target) != EC_Normal)
        {
          DCMTKException(<< "Cant extract Array from DecimalString " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      return EXIT_SUCCESS;
    }
  /** Get a DecimalString Item as a single string
   */
  int  GetElementCS(unsigned short group,
                     unsigned short element,
                     std::string &target,
                     bool throwException = true)
    {
      DcmTagKey tagkey(group,element);
      DcmElement *el;
      if(this->m_Dataset->findAndGetElement(tagkey,el) != EC_Normal)
        {
          DCMTKException(<< "Cant find tag " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      DcmCodeString *csItem = dynamic_cast<DcmCodeString *>(el);
      if(csItem == 0)
        {
          DCMTKException(<< "Cant find DecimalString element " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      OFString ofString;
      if(csItem->getOFStringArray(ofString) != EC_Normal)
        {
        DCMTKException(<< "Can't get DecimalString Value at tag "
                       << std::hex << group << " "
                       << element << std::dec);
        }
      target = "";
      for(unsigned j = 0; j < ofString.length(); ++j)
        {
        target += ofString[j];
        }
      return EXIT_SUCCESS;
    }

  /** get an IS (Integer String Item
   */
  int  GetElementIS(unsigned short group,
                     unsigned short element,
                     int target,
                     bool throwException = true)
    {
      DcmTagKey tagkey(group,element);
      DcmElement *el;
      if(this->m_Dataset->findAndGetElement(tagkey,el) != EC_Normal)
        {
          DCMTKException(<< "Cant find tag " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      DcmIntegerString *isItem = dynamic_cast<DcmIntegerString *>(el);
      if(isItem == 0)
        {
          DCMTKException(<< "Cant find DecimalString element " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      if(isItem->getSint32(target) != EC_Normal)
        {
        DCMTKException(<< "Can't get DecimalString Value at tag "
                       << std::hex << group << " "
                       << element << std::dec);
        }
      return EXIT_SUCCESS;
    }

  /** get an OB OtherByte Item
   */
  int  GetElementOB(unsigned short group,
                     unsigned short element,
                     std::string &target,
                     bool throwException = true)
    {
      DcmTagKey tagkey(group,element);
      DcmElement *el;
      if(this->m_Dataset->findAndGetElement(tagkey,el) != EC_Normal)
        {
          DCMTKException(<< "Cant find tag " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      DcmOtherByteOtherWord *obItem = dynamic_cast<DcmOtherByteOtherWord *>(el);
      if(obItem == 0)
        {
          DCMTKException(<< "Cant find DecimalString element " << std::hex
                                   << group << " " << std::hex
                                   << element << std::dec);
        }
      OFString ofString;
      if(obItem->getOFStringArray(ofString) != EC_Normal)
        {
        DCMTKException(<< "Can't get OFString Value at tag "
                       << std::hex << group << " "
                       << element << std::dec);
        }
      target = "";
      for(unsigned j = 0; j < ofString.length(); ++j)
        {
        target += ofString[j];
        }
      return EXIT_SUCCESS;
    }

  int GetElementSQ(unsigned short group,
                  unsigned short entry,
                  DCMTKSequence &sequence,
                  bool throwException = true)
    {
      DcmSequenceOfItems *seq;
      DcmTagKey tagKey(group,entry);

      if(this->m_Dataset->findAndGetSequence(tagKey,seq) != EC_Normal)
        {
        DCMTKException(<< "Can't find sequence "
                       << std::hex << group << " "
                       << std::hex << entry)
        }
      sequence.SetDcmSequenceOfItems(seq);
      return EXIT_SUCCESS;
    }
  int GetFrameCount() { return this->m_FrameCount; }

  E_TransferSyntax GetTransferSyntax() { return m_Xfer; }
private:
  std::string          m_FileName;
  DcmFileFormat*       m_DFile;
  DcmDataset *         m_Dataset;
  DicomImage *         m_DicomImage;
  E_TransferSyntax     m_Xfer;
  Sint32               m_FrameCount;
};


bool
StringContains(const std::string &string,const std::string pattern)
{
  return string.find(pattern) != std::string::npos;
}

bool swapByteOrder(false);

int
isSystemBigEndian(void)
{
  union
  {
    uint32_t i;
    char c[4];
  } bint = {0x01020304};

  return bint.c[0] == 1;
}

void
AddDictEntry(DcmDictEntry *entry)
{
  DcmDataDictionary &dict = dcmDataDict.wrlock();
  dict.addEntry(entry);
  dcmDataDict.unlock();
}

static unsigned int ExtractSiemensDiffusionInformation(const std::string tagString,
                                                       const std::string nameString,
                                                       std::vector<double>& valueArray )
{
  ::size_t atPosition = tagString.find( nameString );
  while( true )  // skip nameString inside a quotation
    {
    std::string nextChar = tagString.substr( atPosition+nameString.size(), 1 );
    std::cout << nextChar << std::endl;
    if (nextChar.c_str()[0] == 0 )
      {
      break;
      }
    else
      {
      atPosition = tagString.find( nameString, atPosition+2 );
      }
    }

  if ( atPosition == std::string::npos)
    {
    return 0;
    }
  else
    {
    std::string infoAsString = tagString.substr( atPosition, tagString.size()-atPosition+1 );
    const char * infoAsCharPtr = infoAsString.c_str();

    unsigned int vm = *(infoAsCharPtr+64);
    {
    std::string vr = infoAsString.substr( 68, 2 );
    int syngodt = *(infoAsCharPtr+72);
    int nItems = *(infoAsCharPtr+76);
    int localDummy = *(infoAsCharPtr+80);

    //std::cout << "\tName String: " << nameString << std::endl;
    //std::cout << "\tVR: " << vr << std::endl;
    //std::cout << "\tVM: " << vm << std::endl;
    //std::cout << "Local String: " << infoAsString.substr(0,80) << std::endl;

    /* This hack is required for some Siemens VB15 Data */
    if ( ( nameString == "DiffusionGradientDirection" ) && (vr != "FD") )
      {
      bool loop = true;
      while ( loop )
        {
        atPosition = tagString.find( nameString, atPosition+26 );
        if ( atPosition == std::string::npos)
          {
          //std::cout << "\tFailed to find DiffusionGradientDirection Tag - returning" << vm << std::endl;
          return 0;
          }
        infoAsString = tagString.substr( atPosition, tagString.size()-atPosition+1 );
        infoAsCharPtr = infoAsString.c_str();
        //std::cout << "\tOffset to new position" << std::endl;
        //std::cout << "\tNew Local String: " << infoAsString.substr(0,80) << std::endl;
        vm = *(infoAsCharPtr+64);
        vr = infoAsString.substr( 68, 2 );
        if (vr == "FD") loop = false;
        syngodt = *(infoAsCharPtr+72);
        nItems = *(infoAsCharPtr+76);
        localDummy = *(infoAsCharPtr+80);
        //std::cout << "\tVR: " << vr << std::endl;
        //std::cout << "\tVM: " << vm << std::endl;
        }
      }
    else
      {
      //std::cout << "\tUsing initial position" << std::endl;
      }
    //std::cout << "\tArray Length: " << vm << std::endl;
    }

    unsigned int offset = 84;
    for (unsigned int k = 0; k < vm; k++)
      {
      const int itemLength = *(infoAsCharPtr+offset+4);
      const int strideSize = static_cast<int> (ceil(static_cast<double>(itemLength)/4) * 4);
      const std::string valueString = infoAsString.substr( offset+16, itemLength );
      valueArray.push_back( atof(valueString.c_str()) );
      offset += 16+strideSize;
      }
    return vm;
    }
}

/**
 *  Add private tags to the Dicom Dictionary
 */
void
AddFlagsToDictionary()
{
  // relevant GE tags
  static DcmDictEntry GEDictBValue(0x0043, 0x1039, DcmVR(EVR_IS),
                                   "B Value of diffusion weighting", 1, 1, 0,true,
                                   "dicomtonrrd");
  static DcmDictEntry GEDictXGradient(0x0019, 0x10bb, DcmVR(EVR_DS),
                                      "X component of gradient direction", 1, 1 , 0,true,
                                      "dicomtonrrd");
  static DcmDictEntry GEDictYGradient(0x0019, 0x10bc, DcmVR(EVR_DS),
                                      "Y component of gradient direction", 1, 1 , 0,true,
                                      "dicomtonrrd");
  static DcmDictEntry GEDictZGradient(0x0019, 0x10bd, DcmVR(EVR_DS),
                                      "Z component of gradient direction", 1, 1 , 0,true,
                                      "dicomtonrrd");

  // relevant Siemens private tags
  static DcmDictEntry SiemensMosiacParameters(0x0051, 0x100b, DcmVR(EVR_IS),
                                              "Mosiac Matrix Size", 1, 1 , 0,true,
                                              "dicomtonrrd");
  static DcmDictEntry SiemensDictNMosiac(0x0019, 0x100a, DcmVR(EVR_US),
                                         "Number of Images In Mosaic", 1, 1 , 0,true,
                                         "dicomtonrrd");
  static DcmDictEntry SiemensDictBValue(0x0019, 0x100c, DcmVR(EVR_IS),
                                        "B Value of diffusion weighting", 1, 1 , 0,true,
                                        "dicomtonrrd");
  static DcmDictEntry SiemensDictDiffusionDirection(0x0019, 0x100e, DcmVR(EVR_FD),
                                                    "Diffusion Gradient Direction", 3, 3 , 0,true,
                                                    "dicomtonrrd");
  static DcmDictEntry SiemensDictDiffusionMatrix(0x0019, 0x1027, DcmVR(EVR_FD),
                                                 "Diffusion Matrix", 6, 6 , 0,true,
                                                 "dicomtonrrd");
  static DcmDictEntry SiemensDictShadowInfo(0x0029, 0x1010, DcmVR(EVR_OB),
                                            "Siemens DWI Info", 1, 1 , 0,true,
                                            "dicomtonrrd");

  // relevant Philips private tags
  static DcmDictEntry PhilipsDictBValue (0x2001, 0x1003, DcmVR(EVR_FL),
                                         "B Value of diffusion weighting", 1, 1 , 0,true,
                                         "dicomtonrrd");
  static DcmDictEntry PhilipsDictDiffusionDirection  (0x2001, 0x1004, DcmVR(EVR_CS),
                                                      "Diffusion Gradient Direction", 1, 1 , 0,true,
                                                      "dicomtonrrd");
  static DcmDictEntry PhilipsDictDiffusionDirectionRL(0x2005, 0x10b0, DcmVR(EVR_FL),
                                                      "Diffusion Direction R/L", 4, 4 , 0,true,
                                                      "dicomtonrrd");
  static DcmDictEntry PhilipsDictDiffusionDirectionAP(0x2005, 0x10b1, DcmVR(EVR_FL),
                                                      "Diffusion Direction A/P", 4, 4 , 0,true,
                                                      "dicomtonrrd");
  static DcmDictEntry PhilipsDictDiffusionDirectionFH(0x2005, 0x10b2, DcmVR(EVR_FL),
                                                      "Diffusion Direction F/H", 4, 4 , 0,true,
                                                      "dicomtonrrd");

  AddDictEntry(&GEDictBValue);
  AddDictEntry(&GEDictXGradient);
  AddDictEntry(&GEDictYGradient);
  AddDictEntry(&GEDictZGradient);

  // relevant Siemens private tags
  AddDictEntry(&SiemensMosiacParameters);
  AddDictEntry(&SiemensDictNMosiac);
  AddDictEntry(&SiemensDictBValue);
  AddDictEntry(&SiemensDictDiffusionDirection);
  AddDictEntry(&SiemensDictDiffusionMatrix);
  AddDictEntry(&SiemensDictShadowInfo);

  // relevant Philips private tags
  AddDictEntry(&PhilipsDictBValue);
  AddDictEntry(&PhilipsDictDiffusionDirection);
  AddDictEntry(&PhilipsDictDiffusionDirectionRL);
  AddDictEntry(&PhilipsDictDiffusionDirectionAP);
  AddDictEntry(&PhilipsDictDiffusionDirectionFH);

}

typedef short PixelValueType;
typedef itk::Image< PixelValueType, 3 > VolumeType;

int
WriteVolume( VolumeType::Pointer img, const std::string fname )
{
  itk::ImageFileWriter< VolumeType >::Pointer imgWriter =
    itk::ImageFileWriter< VolumeType >::New();

  imgWriter->SetInput( img );
  imgWriter->SetFileName( fname.c_str() );
  try
    {
    imgWriter->Update();
    }
  catch (itk::ExceptionObject &excp)
    {
    std::cerr << "Exception thrown while reading the series" << std::endl;
    std::cerr << excp << std::endl;
    return EXIT_FAILURE;
    }
  return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
  PARSE_ARGS;

  typedef itk::ImageSeriesReader< VolumeType > ReaderType;
  typedef itk::GDCMSeriesFileNames             InputNamesGeneratorType;

  AddFlagsToDictionary();
  bool nrrdFormat(true);
  if(inputDicomDirectory == "")
    {
    std::cerr << "Missing DICOM input directory path" << std::endl;
    return EXIT_FAILURE;
    }

  if(outputVolume == "")
    {
    std::cerr << "Missing DICOM output volume name" << std::endl;
    return EXIT_FAILURE;
    }

  std::string outputVolumeHeaderName(outputVolume);
  if(outputVolume.find("/") == std::string::npos &&
     outputVolume.find("\\") == std::string::npos)
    {
    if(outputVolumeHeaderName.size() != 0)
      {
      outputVolumeHeaderName = outputDirectory;
      outputVolumeHeaderName += "/";
      outputVolumeHeaderName + outputVolume;
      }
    }

  std::string outputVolumeDataName;
  const size_t extensionPos = outputVolumeHeaderName.find(".nhdr");
  if(extensionPos != std::string::npos)
    {
    outputVolumeDataName = outputVolumeHeaderName.substr(0,extensionPos);
    outputVolumeDataName += ".raw";
    nrrdFormat = false;
    }

  //
  // get the names of all slices in the directory
  InputNamesGeneratorType::Pointer inputNames = InputNamesGeneratorType::New();
  inputNames->SetLoadSequences( true );
  inputNames->SetLoadPrivateTags( true );
  inputNames->SetInputDirectory(inputDicomDirectory);

  itk::FilenamesContainer inputFileNames(inputNames->GetInputFileNames());
  if(inputFileNames.size() < 1)
    {
    std::cerr << "Error: no DICOMfiles found in inputDirectory: " << inputDicomDirectory
              << std::endl;
    return EXIT_FAILURE;
    }
  else if(inputFileNames.size() == 1)
    {
    //
    // Not sure this code makes any sense, or ever gets called
    // actually. It's asking itksys::Directory to open up a file
    // as a directory.
    inputFileNames.resize( 0 );
    itksys::Directory directory;
    directory.Load( itksys::SystemTools::CollapseFullPath(inputDicomDirectory.c_str()).c_str() );
    typedef itk::GDCMImageIO ImageIOType;
    ImageIOType::Pointer gdcmIOTest = ImageIOType::New();

    // for each patient directory
    for ( unsigned int k = 0; k < directory.GetNumberOfFiles(); k++)
      {
      std::string subdirectory( inputDicomDirectory.c_str() );
      subdirectory = subdirectory + "/" + directory.GetFile(k);

      const std::string sqDir( directory.GetFile(k) );
      if (sqDir.length() == 1 && directory.GetFile(k)[0] == '.')   // skip self
        {
        continue;
        }
      else if (sqDir.length() == 2 && sqDir.find( ".." ) != std::string::npos)    // skip parent
        {
        continue;
        }
      else if (!itksys::SystemTools::FileIsDirectory( subdirectory.c_str() ))     // load only files
        {
        if ( gdcmIOTest->CanReadFile(subdirectory.c_str()) )
          {
          inputFileNames.push_back( subdirectory );
          }
        }
      }
    }

  DCMTKFileReader dcmFileReader;
  dcmFileReader.SetFileName(inputFileNames[0]);
  try
    {
    dcmFileReader.LoadFile();
    }
  catch(itk::ExceptionObject &excp)
    {
    std::cerr << "Exception thrown while reading first file in series" << std::endl;
    std::cerr << excp << std::endl;
    return EXIT_FAILURE;
    };

  std::string vendor;
  try
    {
    dcmFileReader.GetElementLO(0x0008,0x0070,vendor);
    }
  catch(itk::ExceptionObject &excp)
    {
    std::cerr << "Can't get vendor name from DICOM file" << excp << std::endl;
    return EXIT_FAILURE;
    }


  std::string modality;
  try
    {
    dcmFileReader.GetElementCS(0x0008,0x0060,modality);
    }
  catch(itk::ExceptionObject &excp)
    {
    std::cerr << "Can't find modality in DICOM file" << excp << std::endl;
    return EXIT_FAILURE;
    }

  //
  // IF it's a PET or SPECT file, just write it out as a float image.
  if(StringContains(modality,"PT") || StringContains(modality,"ST"))
    {
    typedef itk::Image<float, 3> USVolumeType;
    itk::ImageSeriesReader<USVolumeType>::Pointer seriesReader =
      itk::ImageSeriesReader<USVolumeType>::New();
    seriesReader->SetFileNames( inputFileNames );

    itk::ImageFileWriter<USVolumeType>::Pointer nrrdImageWriter =
      itk::ImageFileWriter<USVolumeType>::New();

    nrrdImageWriter->SetFileName( outputVolumeHeaderName );
    nrrdImageWriter->SetInput( seriesReader->GetOutput() );
    try
      {
      nrrdImageWriter->Update();
      }
    catch( itk::ExceptionObject & err )
      {
      std::cerr << "ExceptionObject caught !" << std::endl;
      std::cerr << err << std::endl;
      return EXIT_FAILURE;
      }
    return EXIT_SUCCESS;
    }

  try
    {
    std::string ImageType;
    dcmFileReader.GetElementCS(0x0008,0x0008, ImageType);

    bool SliceMosaic = vendor.find("SIEMENS") != std::string::npos &&
      ImageType.find("MOSAIC") != std::string::npos;

    //////////////////////////////////////////////////
    // 1) Read the input series as an array of slices
    ReaderType::Pointer reader = ReaderType::New();
    itk::GDCMImageIO::Pointer gdcmIO = itk::GDCMImageIO::New();
    reader->SetImageIO( gdcmIO );
    reader->SetFileNames( inputFileNames );
    const unsigned int nSlice = inputFileNames.size();
    try
      {
      reader->Update();
      }
    catch (itk::ExceptionObject &excp)
      {
      std::cerr << "Exception thrown while reading the series" << std::endl;
      std::cerr << excp << std::endl;
      return EXIT_FAILURE;
      }
    VolumeType::Pointer dmImage = VolumeType::New();

    //////////////////////////////////////////////////
    // 1-A) Read the input dicom headers
    std::vector<DCMTKFileReader> allHeaders(inputFileNames.size());
    for(unsigned i = 0; i < allHeaders.size(); ++i)
      {
      allHeaders[i].SetFileName(inputFileNames[i]);
      try
        {
        allHeaders[i].LoadFile();
        }
      catch(...)
        {
        std::cerr << "Error reading slice" << inputFileNames[i] << std::endl;
        return EXIT_FAILURE;
        }
      }

    // get image dims and resolution
    unsigned short nRows, nCols;
    dcmFileReader.GetElementUS(0x0028,0x0010,nRows);
    dcmFileReader.GetElementUS(0x0028,0x0011,nCols);

    double xRes, yRes;
    {
    double res[2];
    dcmFileReader.GetElementDS(0x0028,0x0030,2,res);
    xRes = res[0]; yRes = res[1];
    }

    itk::Vector<double,3> ImageOrigin;
    {
    double origin[3];
    dcmFileReader.GetElementDS(0x0020, 0x0032, 3,origin);
    ImageOrigin[0] = origin[0];
    ImageOrigin[1] = origin[1];
    ImageOrigin[2] = origin[2];
    }

    double sliceSpacing;
    dcmFileReader.GetElementDS(0x0018, 0x0088, 1, &sliceSpacing);

    // Make a hash of the sliceLocations in order to get the correct
    // count.  This is more reliable since SliceLocation may not be available.
    std::map<std::string,int> sliceLocations;
    std::vector<int> sliceLocationIndicator;
    std::vector<std::string> sliceLocationStrings;

    sliceLocationIndicator.resize( nSlice );

    for (unsigned int k = 0; k < nSlice; k++)
      {
      std::string originString;

      allHeaders[k].GetElementDS(0x0020, 0x0032, originString );
      sliceLocationStrings.push_back( originString );
      sliceLocations[originString]++;
      }

    for (unsigned int k = 0; k < nSlice; k++)
      {
      std::map<std::string,int>::iterator it = sliceLocations.find( sliceLocationStrings[k] );
      sliceLocationIndicator[k] = distance( sliceLocations.begin(), it );
      }

    unsigned int numberOfSlicesPerVolume=sliceLocations.size();
    std::cout << "=================== numberOfSlicesPerVolume:" << numberOfSlicesPerVolume << std::endl;

    if ( nSlice >= 2)
      {
      if(sliceLocationIndicator[0] != sliceLocationIndicator[1])
        {
        std::cout << "Dicom images are ordered in a volume interleaving way." << std::endl;
        }
      else
        {
        std::cout << "Dicom images are ordered in a slice interleaving way." << std::endl;
        // reorder slices into a volume interleaving manner
        int Ns = numberOfSlicesPerVolume;
        int Nv = nSlice / Ns; // do we need to do error check here

        VolumeType::RegionType R = reader->GetOutput()->GetLargestPossibleRegion();
        R.SetSize(2,1);
        std::vector<VolumeType::PixelType> v(nSlice);
        std::vector<VolumeType::PixelType> w(nSlice);

        itk::ImageRegionIteratorWithIndex<VolumeType> I( reader->GetOutput(), R );
        for (I.GoToBegin(); !I.IsAtEnd(); ++I)
          {
          VolumeType::IndexType idx = I.GetIndex();

          // extract all values in one "column"
          for (unsigned int k = 0; k < nSlice; k++)
            {
            idx[2] = k;
            v[k] = reader->GetOutput()->GetPixel( idx );
            }

          // permute
          for (int k = 0; k < Nv; k++)
            {
            for (int m = 0; m < Ns; m++)
              {
              w[k*Ns+m] = v[m*Nv+k];
              }
            }

          // put things back in order
          for (unsigned int k = 0; k < nSlice; k++)
            {
            idx[2] = k;
            reader->GetOutput()->SetPixel( idx, w[k] );
            }
          }
        }
      }
    itk::Matrix<double,3,3> MeasurementFrame;
    MeasurementFrame.SetIdentity();

    // check ImageOrientationPatient and figure out slice direction in
    // L-P-I (right-handed) system.
    // In Dicom, the coordinate frame is L-P by default. Look at
    // http://medical.nema.org/dicom/2007/07_03pu.pdf ,  page 301
    itk::Matrix<double,3,3> LPSDirCos;

    {
    double dirCosArray[6];
    dcmFileReader.GetElementDS(0x0020, 0x0037, 6, dirCosArray);
    double *dirCosArrayP = dirCosArray;
    for(unsigned i = 0; i < 2; ++i)
      {
      for(unsigned j = 0; j < 3; ++j,++dirCosArrayP)
        {
        LPSDirCos[j][i] = *dirCosArrayP;
        }
      }
    }

    // Cross product, this gives I-axis direction
    LPSDirCos[0][2] = (LPSDirCos[1][0]*LPSDirCos[2][1]-LPSDirCos[2][0]*LPSDirCos[1][1]);
    LPSDirCos[1][2] = (LPSDirCos[2][0]*LPSDirCos[0][1]-LPSDirCos[0][0]*LPSDirCos[2][1]);
    LPSDirCos[2][2] = (LPSDirCos[0][0]*LPSDirCos[1][1]-LPSDirCos[1][0]*LPSDirCos[0][1]);

    std::cout << "ImageOrientationPatient (0020:0037): ";
    std::cout << "LPS Orientation Matrix" << std::endl;
    std::cout << LPSDirCos << std::endl;

    itk::Matrix<double,3,3> SpacingMatrix;
    SpacingMatrix.Fill(0.0);
    SpacingMatrix[0][0]=xRes;
    SpacingMatrix[1][1]=yRes;
    SpacingMatrix[2][2]=sliceSpacing;
    std::cout << "SpacingMatrix" << std::endl;
    std::cout << SpacingMatrix << std::endl;

    itk::Matrix<double,3,3> OrientationMatrix;
    OrientationMatrix.SetIdentity();

    itk::Matrix<double,3,3> NRRDSpaceDirection;
    std::string nrrdSpaceDefinition="left-posterior-superior";;
    NRRDSpaceDirection=LPSDirCos*OrientationMatrix*SpacingMatrix;

    std::cout << "NRRDSpaceDirection" << std::endl;
    std::cout << NRRDSpaceDirection << std::endl;

    unsigned int mMosaic = 0;   // number of raws in each mosaic block;
    unsigned int nMosaic = 0;   // number of columns in each mosaic block
    unsigned int nSliceInVolume = 0;
    unsigned int nVolume = 0;
    bool SliceOrderIS(true);

    // figure out slice order and mosaic arrangement.
    if ( vendor.find("GE") != std::string::npos || (vendor.find("SIEMENS") != std::string::npos && !SliceMosaic) )
      {
      if(vendor.find("GE") != std::string::npos)
        {
        MeasurementFrame=LPSDirCos;
        }
      else //SIEMENS data assumes a measurement frame that is the identity matrix.
        {
        MeasurementFrame.SetIdentity();
        }
      // has the measurement frame represented as an identity matrix.
      double image0Origin[3];
      allHeaders[0].GetElementDS(0x0020, 0x0032, 3, image0Origin);
      std::cout << "Slice 0: " << image0Origin[0] << " " << image0Origin[1] << " " << image0Origin[2] << std::endl;

      // assume volume interleaving, i.e. the second dicom file stores
      // the second slice in the same volume as the first dicom file
      double image1Origin[3];
      allHeaders[1].GetElementDS(0x0020, 0x0032, 3, image1Origin);
      std::cout << "Slice 0: " << image1Origin[0] << " " << image1Origin[1] << " " << image1Origin[2] << std::endl;

      image1Origin[0] -= image0Origin[0];
      image1Origin[1] -= image0Origin[1];
      image1Origin[2] -= image0Origin[2];
      double x1 = image1Origin[0]*(NRRDSpaceDirection[0][2]) +
        image1Origin[1]*(NRRDSpaceDirection[1][2]) +
        image1Origin[2]*(NRRDSpaceDirection[2][2]);
      if (x1 < 0)
        {
        SliceOrderIS = false;
        }
      }
    else if ( vendor.find("SIEMENS") != std::string::npos && SliceMosaic )
      {
      MeasurementFrame.SetIdentity(); //The DICOM version of SIEMENS that uses private tags
      // has the measurement frame represented as an identity matrix.
      std::cout << "Siemens SliceMosaic......" << std::endl;

      SliceOrderIS = false;

      // for siemens mosaic image, figure out mosaic slice order from 0029|1010
      // copy information stored in 0029,1010 into a string for parsing
      std::string tag;
      dcmFileReader.GetElementOB(0x0029,0x1010, tag);

      // parse SliceNormalVector from 0029,1010 tag
      std::vector<double> valueArray(0);
      int nItems = ExtractSiemensDiffusionInformation(tag, "SliceNormalVector", valueArray);
      if (nItems != 3)  // did not find enough information
        {
        std::cout << "Warning: Cannot find complete information on SliceNormalVector in 0029|1010" << std::endl;
        std::cout << "         Slice order may be wrong." << std::endl;
        }
      else if (valueArray[2] > 0)
        {
        SliceOrderIS = true;
        }

      // parse NumberOfImagesInMosaic from 0029,1010 tag
      valueArray.resize(0);
      nItems = ExtractSiemensDiffusionInformation(tag, "NumberOfImagesInMosaic", valueArray);
      if (nItems == 0)  // did not find enough information
        {
        std::cout << "Warning: Cannot find complete information on NumberOfImagesInMosaic in 0029|1010" << std:: endl;
        std::cout << "         Resulting image may contain empty slices." << std::endl;
        }
      else
        {
        nSliceInVolume = static_cast<int>(valueArray[0]);
        mMosaic = static_cast<int> (ceil(sqrt(valueArray[0])));
        nMosaic = mMosaic;
        }
      std::cout << "Mosaic in " << mMosaic << " X " << nMosaic
                << " blocks (total number of blocks = " << valueArray[0] << ")." << std::endl;
      }
    else if ( vendor.find("PHILIPS") != std::string::npos && nSlice > 1)
      // so this is not a philips multi-frame single dicom file
      {
      MeasurementFrame=LPSDirCos; //Philips oblique scans list the gradients with respect to the ImagePatientOrientation.
      SliceOrderIS = true;

      nSliceInVolume = numberOfSlicesPerVolume;
      nVolume = nSlice/nSliceInVolume;

      double  image0Origin[3];
      allHeaders[0].GetElementDS(0x0020, 0x0032, 3, image0Origin);
      std::cout << "Slice 0: " << image0Origin[0] << " " << image0Origin[1] << " " << image0Origin[2] << std::endl;

      // assume volume interleaving, i.e. the second dicom file stores
      // the second slice in the same volume as the first dicom file
      double  image1Origin[3];
      allHeaders[nVolume].GetElementDS(0x0020, 0x0032, 3, image1Origin);
      std::cout << "Slice " << nVolume << ": " << image1Origin[0] << " "
                << image1Origin[1] << " " << image1Origin[2] << std::endl;

      image1Origin[0] -= image0Origin[0];
      image1Origin[1] -= image0Origin[1];
      image1Origin[2] -= image0Origin[2];
      double x0 = image1Origin[0] * (NRRDSpaceDirection[0][2]) +
        image1Origin[1] * (NRRDSpaceDirection[1][2]) +
        image1Origin[2] * (NRRDSpaceDirection[2][2]);

      // VAM - This needs more investigation -
      // Should we default to false and change based on slice order
      if (x0 < 0)
        {
        SliceOrderIS = false;
        }
      }
    else if ( vendor.find("PHILIPS") != std::string::npos && nSlice == 1)
      {
      // special handling for philips multi-frame dicom later.
      }
    else
      {
      std::cout << " Warning: vendor type not valid" << std::endl;
      // treate the dicom series as an ordinary image and write a straight nrrd file.
      WriteVolume( reader->GetOutput(), outputVolumeHeaderName );
      return EXIT_SUCCESS;
      }

    if ( SliceOrderIS )
      {
      std::cout << "Slice order is IS" << std::endl;
      }
    else
      {
      std::cout << "Slice order is SI" << std::endl;
      NRRDSpaceDirection[0][2] = -NRRDSpaceDirection[0][2];
      NRRDSpaceDirection[1][2] = -NRRDSpaceDirection[1][2];
      NRRDSpaceDirection[2][2] = -NRRDSpaceDirection[2][2];
      }

    std::cout << "Row: " << (NRRDSpaceDirection[0][0])  << ", "
              << (NRRDSpaceDirection[1][0]) << ", "
              << (NRRDSpaceDirection[2][0]) << std::endl;
    std::cout << "Col: " << (NRRDSpaceDirection[0][1])
              << ", " << (NRRDSpaceDirection[1][1])
              << ", " << (NRRDSpaceDirection[2][1]) << std::endl;
    std::cout << "Sli: " << (NRRDSpaceDirection[0][2])
              << ", " << (NRRDSpaceDirection[1][2]) << ", "
              << (NRRDSpaceDirection[2][2]) << std::endl;

    const float orthoSliceSpacing = fabs((NRRDSpaceDirection[2][2]));

    int nIgnoreVolume = 0; // Used for Philips Trace like images
    std::vector<int> useVolume;

    std::vector<float> bValues(0);
    float maxBvalue = 0;
    int nBaseline = 0;

    // UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem is only of debug purposes.
    std::vector< vnl_vector_fixed<double, 3> > DiffusionVectors;
    std::vector< vnl_vector_fixed<double, 3> > UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem;
    std::vector< unsigned int>  bad_gradient_indices;
    std::vector<int> ignorePhilipsSliceMultiFrame;
    ////////////////////////////////////////////////////////////
    // vendor dependent tags.
    // read in gradient vectors and determin nBaseline and nMeasurement

    if ( vendor.find("GE") != std::string::npos )
      {
      nSliceInVolume = numberOfSlicesPerVolume;
      nVolume = nSlice/nSliceInVolume;

      // assume volume interleaving
      std::cout << "Number of Slices: " << nSlice << std::endl;
      std::cout << "Number of Volume: " << nVolume << std::endl;
      std::cout << "Number of Slices in each volume: " << nSliceInVolume << std::endl;

      for (unsigned int k = 0; k < nSlice; k += nSliceInVolume)
        {
        // parsing bvalue and gradient directions
        vnl_vector_fixed<double, 3> vect3d;
        vect3d.fill( 0 );
        // for some weird reason this item in the GE dicom
        // header is stored as an IS (Integer String) element.
        int intb;
        allHeaders[k].GetElementIS(0x0043, 0x1039, intb);
        float b = static_cast<float>(intb);

        allHeaders[k].GetElementDS(0x0019, 0x10bb, 1, &vect3d[0]);

        allHeaders[k].GetElementDS(0x0019, 0x10bc, 1, &vect3d[1]);

        allHeaders[k].GetElementDS(0x0019, 0x10bd, 1, &vect3d[2]);

        bValues.push_back( b );
        if (b == 0)
          {
          vect3d.fill( 0 );
          UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
          DiffusionVectors.push_back(vect3d);
          }
        else
          {
          UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
          // vect3d.normalize();
          DiffusionVectors.push_back(vect3d);
          }

        std::cout << "B-value: " << b <<
          "; diffusion direction: " << vect3d[0] << ", " << vect3d[1] << ", " << vect3d[2] << std::endl;
        }
      }
    else if ( vendor.find("PHILIPS") != std::string::npos && nSlice > 1 )
      {
      // assume volume interleaving
      std::cout << "Number of Slices: " << nSlice << std::endl;
      std::cout << "Number of Volumes: " << nVolume << std::endl;
      std::cout << "Number of Slices in each volume: " << nSliceInVolume << std::endl;

      std::string tmpString = "";
      //NOTE:  Philips interleaves the directions, so the all gradient directions can be
      //determined in the first "nVolume" slices which represents the first slice from each
      //of the gradient volumes.
      for (unsigned int k = 0; k < nVolume; k++ /*nSliceInVolume*/)
        {
        std::string DiffusionDirectionality;
        const bool useSuppplement49Definitions =
          allHeaders[k].GetElementCS(0x0018,0x9075,DiffusionDirectionality,false) == EXIT_SUCCESS;

        bool B0FieldFound = false;
        double b=0.0;
        if (useSuppplement49Definitions == true )
          {
          B0FieldFound = allHeaders[k].GetElementFD(0x0018,0x9087,b,false) == EXIT_SUCCESS;
          }
        else
          {
          float floatB;
          B0FieldFound = allHeaders[k].GetElementFL(0x2001,0x1003,floatB,false) == EXIT_SUCCESS;
          b = static_cast<double>(floatB);
          std::string tag;
          allHeaders[k].GetElementCS(0x2001, 0x1004, tag );
          if((tag.find("I") != std::string::npos) && (b != 0) )
            {
            DiffusionDirectionality="ISOTROPIC";
            }
          }

        vnl_vector_fixed<double, 3> vect3d;
        vect3d.fill( 0 );
        //std::cout << "HACK: " << "DiffusionDirectionality=" << DiffusionDirectionality << ", k= " <<  k << std::endl;
        //std::cout << "HACK: " << "B0FieldFound=" << B0FieldFound << ", b=" << b << ", DiffusionDirectionality=" << DiffusionDirectionality << std::endl;

        if ( DiffusionDirectionality.find("ISOTROPIC") != std::string::npos )
          { //Deal with images that are to be ignored
          //std::cout << " SKIPPING ISOTROPIC Diffusion. " << std::endl;
          //std::cout << "HACK: IGNORE IMAGEFILE:   " << k << " of " << filenames.size() << " " << filenames[k] << std::endl;
          // Ignore the Trace like image
          nIgnoreVolume++;
          useVolume.push_back(0);
          continue;
          }
        else if (( !B0FieldFound || b == 0 ) || ( DiffusionDirectionality.find("NONE") != std::string::npos )  )
          { //Deal with b0 images
          bValues.push_back(b);
          UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
          DiffusionVectors.push_back(vect3d);
          useVolume.push_back(1);
          continue;
          }

        else if (DiffusionDirectionality.find("DIRECTIONAL") != std::string::npos || ( DiffusionDirectionality == "" ))
          { //Deal with gradient direction images
          bValues.push_back(b);
          //std::cout << "HACK: GRADIENT IMAGEFILE: " << k << " of " << filenames.size() << " " << filenames[k] << std::endl;
          useVolume.push_back(1);
          if (useSuppplement49Definitions == true )
            {
            double *doubleArray;
            //Use alternate method to get value out of a sequence header (Some Phillips Data).
            if(allHeaders[k].GetElementFD(0x0018, 0x9089, doubleArray,false) != EXIT_SUCCESS)
              {
              //std::cout << "Looking for  0018|9089 in sequence 0018,9076" << std::endl;
              // gdcm::SeqEntry *
              // DiffusionSeqEntry=allHeaders[k]->GetSeqEntry(0x0018,0x9076);
              DCMTKSequence DiffusionSeqEntry;
              allHeaders[k].GetElementSQ(0x0018,0x9076,DiffusionSeqEntry);
              // const unsigned int
              // n=DiffusionSeqEntry->GetNumberOfSQItems();
              unsigned int n = DiffusionSeqEntry.card();
              if( n == 0 )
                {
                std::cout << "ERROR:  Sequence entry 0018|9076 has no items." << std::endl;
                return EXIT_FAILURE;
                }
              DiffusionSeqEntry.GetElementFD(0x0018,0x9089, doubleArray);
              }
            vect3d[0] = doubleArray[0];
            vect3d[1] = doubleArray[1];
            vect3d[2] = doubleArray[2];
            std::cout << "===== gradient orientations:" << k << " "
                      << inputFileNames[k] << " (0018,9089) " << " " << vect3d << std::endl;
            }
          else
            {
            float tmp[3];
            /*const bool b0exist =*/
            allHeaders[k].GetElementFL( 0x2005, 0x10b0, tmp[0] );
            allHeaders[k].GetElementFL( 0x2005, 0x10b1, tmp[1] );
            allHeaders[k].GetElementFL( 0x2005, 0x10b2, tmp[2] );
            vect3d[0] = static_cast<double>(tmp[0]);
            vect3d[1] = static_cast<double>(tmp[1]);
            vect3d[2] = static_cast<double>(tmp[2]);
            }

          UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
          // vect3d.normalize();
          DiffusionVectors.push_back(vect3d);
          }
        else // Have no idea why we'd be here so error out
          {
          std::cout << "ERROR: DiffusionDirectionality was "
                    << DiffusionDirectionality << "  Don't know what to do with that..." << std::endl;
          return EXIT_FAILURE;
          }

        std::cout << "DiffusionDirectionality: " << DiffusionDirectionality
                  << " :B-Value " << b << " :DiffusionOrientation " << vect3d
                  << " :Filename " << inputFileNames[k] << std::endl;
        }
      }
    else if ( vendor.find("SIEMENS") != std::string::npos )
      {

      int nStride = 1;
      if ( !SliceMosaic )
        {
        std::cout << orthoSliceSpacing << std::endl;
        nSliceInVolume = numberOfSlicesPerVolume;
        nVolume = nSlice/nSliceInVolume;
        std::cout << "Number of Slices: " << nSlice << std::endl;
        std::cout << "Number of Volume: " << nVolume << std::endl;
        std::cout << "Number of Slices in each volume: " << nSliceInVolume << std::endl;
        nStride = nSliceInVolume;
        }
      else
        {
        std::cout << "Data in Siemens Mosaic Format" << std::endl;
        nVolume = nSlice;
        std::cout << "Number of Volume: " << nVolume << std::endl;
        std::cout << "Number of Slices in each volume: " << nSliceInVolume << std::endl;
        nStride = 1;
        }

      // JTM - Determine bvalues from all gradients
      double max_bValue = 0.0;
      vnl_vector_fixed<double, 3> vect3d;

      for (unsigned int k = 0; k < nSlice; k += nStride )
        {
        std::string diffusionInfoString;;
        allHeaders[k].GetElementOB( 0x0029, 0x1010, diffusionInfoString );

        // parse B_value from 0029,1010 tag
        std::vector<double> valueArray(0);
        int nItems = ExtractSiemensDiffusionInformation(diffusionInfoString, "B_value", valueArray);

        if (nItems != 1)   // did not find enough information
          {
          std::cout << "Warning: Cannot find complete information on B_value in 0029|1010" << std::endl;
          bValues.push_back( 0.0 );
          vect3d.fill( 0.0 );
          UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
          DiffusionVectors.push_back(vect3d);
          continue;
          }
        else
          {
          // JTM - Patch from UNC: fill the nhdr header with the gradient directions and
          // bvalues computed out of the BMatrix
          valueArray.resize(0);
          int nItems = ExtractSiemensDiffusionInformation(diffusionInfoString, "B_matrix", valueArray);
          vnl_matrix_fixed<double, 3, 3> bMatrix;

          if ((useBMatrixGradientDirections) && (nItems == 6))
            {
            std::cout << "=============================================" << std::endl;
            std::cout << "BMatrix calculations..." << std::endl;
            // UNC comments: We get the value of the b-value tag in the header.
            // We won't use it as is, but just to locate the B0 images.
            // This check must be added, otherwise the bmatrix of the B0 is not
            // read properly (it's not an actual field in the DICOM header of the B0).
            std::vector<double> bval_tmp(0);
            bool b0_image = false;

            // UNC comments: Get the bvalue
            nItems = ExtractSiemensDiffusionInformation(diffusionInfoString, "B_value", bval_tmp);
            if (bval_tmp[0] == 0)
              {
              b0_image = true;
              }

            // UNC comments: The principal eigenvector of the bmatrix is to be extracted as
            // it's the gradient direction and trace of the matrix is the b-value

            double bvalue = 0;

            // UNC comments: Fill out the 3x3 bmatrix with the 6 components read from the
            // DICOM header.
            bMatrix[0][0] = valueArray[0];
            bMatrix[0][1] = valueArray[1];
            bMatrix[0][2] = valueArray[2];
            bMatrix[1][1] = valueArray[3];
            bMatrix[1][2] = valueArray[4];
            bMatrix[2][2] = valueArray[5];
            bMatrix[1][0] = bMatrix[0][1];
            bMatrix[2][0] = bMatrix[0][2];
            bMatrix[2][1] = bMatrix[1][2];

            // UNC comments: Computing the decomposition
            vnl_svd<double> svd(bMatrix);

            // UNC comments: Extracting the principal eigenvector i.e. the gradient direction
            vect3d[0] = svd.U(0,0);
            vect3d[1] = svd.U(1,0);
            vect3d[2] = svd.U(2,0);

            std::cout << "BMatrix: " << std::endl;
            std::cout << bMatrix[0][0] << std::endl;
            std::cout << bMatrix[0][1] << "\t" << bMatrix[1][1] << std::endl;
            std::cout << bMatrix[0][2] << "\t" << bMatrix[1][2] << "\t" << bMatrix[2][2] << std::endl;

            // UNC comments: The b-value si the trace of the bmatrix
            bvalue = bMatrix[0][0] + bMatrix[1][1] + bMatrix[2][2];
            std::cout << bvalue << std::endl;
            // UNC comments: Even if the bmatrix is null, the svd decomposition set the 1st eigenvector
            // to (1,0,0). So we force the gradient direction to 0 if the bvalue is null
            if((b0_image == true) || (bvalue == 0))
              {
              std::cout << "B0 image detected: gradient direction and bvalue forced to 0" << std::endl;
              vect3d[0] = 0;
              vect3d[1] = 0;
              vect3d[2] = 0;
              std::cout << "Gradient coordinates: " << vect3d[0] << " " << vect3d[1] << " " << vect3d[2] << std::endl;
              bValues.push_back(0);
              }
            else
              {
              std::cout << "Gradient coordinates: " << vect3d[0] << " " << vect3d[1] << " " << vect3d[2] << std::endl;
              bValues.push_back(bvalue);
              }
            DiffusionVectors.push_back(vect3d);
            }
          valueArray.resize(0);
          ExtractSiemensDiffusionInformation(diffusionInfoString, "B_value", valueArray);
          bValues.push_back( valueArray[0] );
          }

        if (bValues[k] > max_bValue)
          {
          max_bValue = bValues[k];
          }
        }


      // JTM - Create gradient scaling factor, which is determined by the largest b
      // value in the scan
      std::vector<double> gradient_scaling_factor;

      if(useBMatrixGradientDirections == false)
        {
        for (unsigned int k = 0; k < nSlice; k+=nStride)
          {
          double scaling_factor = bValues[k] / max_bValue;
          gradient_scaling_factor.push_back(scaling_factor);
          }

        for (unsigned int k = 0; k < nSlice; k += nStride )
          {
          std::cout << "=======================================" << std::endl << std::endl;
          std::string diffusionInfoString;
          allHeaders[k].GetElementOB(0x0029, 0x1010, diffusionInfoString );

          std::vector<double> valueArray;
          vnl_vector_fixed<double, 3> vect3d;

          // parse DiffusionGradientDirection from 0029,1010 tag
          valueArray.resize(0);
          int nItems = ExtractSiemensDiffusionInformation(diffusionInfoString, "DiffusionGradientDirection", valueArray);
          std::cout << "Number of Directions : " << nItems << std::endl;
          std::cout << "   Directions 0: " << valueArray[0] << std::endl;
          std::cout << "   Directions 1: " << valueArray[1] << std::endl;
          std::cout << "   Directions 2: " << valueArray[2] << std::endl;
          if (nItems != 3)  // did not find enough information
            {
            std::cout << "Warning: Cannot find complete information on DiffusionGradientDirection in 0029|1010" << std::endl;
            vect3d.fill( 0 );
            UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
            DiffusionVectors.push_back(vect3d);
            }
          else
            {
            double DiffusionVector_magnitude;
            double DiffusionVector_magnitude_difference = 0.0;

            vect3d[0] = valueArray[0];
            vect3d[1] = valueArray[1];
            vect3d[2] = valueArray[2];

            DiffusionVector_magnitude = sqrt((vect3d[0]*vect3d[0]) + (vect3d[1]*vect3d[1]) + (vect3d[2]*vect3d[2]));

            if (gradient_scaling_factor[k] != 0.0)
              {
              DiffusionVector_magnitude_difference = fabs(1.0 - (DiffusionVector_magnitude / gradient_scaling_factor[k]));
              std::cout << "DiffusionVector_magnitude_difference " << DiffusionVector_magnitude_difference << std::endl;
              std::cout << "gradient_scaling_factor " << gradient_scaling_factor[k] << std::endl;
              std::cout << "DiffusionVector_magnitude " << DiffusionVector_magnitude << std::endl;
              if ((DiffusionVector_magnitude > 0.0) &&
                  (DiffusionVector_magnitude_difference > smallGradientThreshold) && (!useBMatrixGradientDirections))
                {
                std::cout << "ERROR: Gradient vector with unreasonably small magnitude exists." << std::endl;
                std::cout << "Gradient #" << k << " with magnitude " << DiffusionVector_magnitude << std::endl;
                std::cout << "Please set useBMatrixGradientDirections to calculate gradient directions "
                          << "from the scanner B Matrix to alleviate this problem." << std::endl;
                return EXIT_FAILURE;
                }
              }

            UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
            // vect3d.normalize();
            DiffusionVectors.push_back(vect3d);
            int p = bValues.size();
            std::cout << "Image#: " << k << " BV: " << bValues[p-1] << " GD: " << DiffusionVectors[k] << std::endl;
            }
          }
        }
      }
    else if (vendor.find("PHILIPS") != std::string::npos && nSlice == 1) // multi-frame file, everything is inside
      {

      std::map<std::vector<double>, double> gradientDirectionAndBValue;
      ignorePhilipsSliceMultiFrame.clear();

      sliceLocations.clear();
      bValues.clear();
      DiffusionVectors.clear();
      useVolume.clear();

      DCMTKSequence perFrameFunctionalGroup;
      DCMTKSequence innerSeq;
      double dwbValue;

      dcmFileReader.GetElementSQ(0x5200,0x9230,perFrameFunctionalGroup);
      int nItems = perFrameFunctionalGroup.card();

      for(unsigned long i = 0; 
          i < static_cast<unsigned long>(perFrameFunctionalGroup.card()); ++i)
        {
        DCMTKSequence curSequence;
        perFrameFunctionalGroup.GetSequence(i,curSequence);
        // index slice locations with string origin
        {
        DCMTKSequence originSeq;
        curSequence.GetElementSQ(0x0020, 0x9113,originSeq);
        originSeq.GetSequence(0,innerSeq);
        std::string originString;
        innerSeq.GetElementDS(0x0020,0x0032,originString);
        sliceLocations[originString]++;
        }

        std::string dirValue;
        {
        DCMTKSequence mrDiffusionSeq;
        curSequence.GetElementSQ(0x0018,0x9117,mrDiffusionSeq);
        mrDiffusionSeq.GetSequence(0,innerSeq);
        innerSeq.GetElementCS(0x0018,0x9075,dirValue);
        }

        if ( dirValue.find("ISO") != std::string::npos )
          {
          useVolume.push_back(0);
          ignorePhilipsSliceMultiFrame.push_back( i );
          }
        else if (dirValue.find("NONE") != std::string::npos)
          {
          useVolume.push_back(1);
          std::vector<double> v(3);
          v[0] = 0; v[1] = 0; v[2] = 0;
          unsigned int nOld = gradientDirectionAndBValue.size();
          gradientDirectionAndBValue[v] = 0;
          unsigned int nNew = gradientDirectionAndBValue.size();

          if (nOld != nNew)
            {
            vnl_vector_fixed<double, 3> vect3d;
            vect3d.fill( 0 );
            DiffusionVectors.push_back( vect3d );
            UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
            bValues.push_back( 0 );

            }
          }
        else
          {
          useVolume.push_back(1);
          innerSeq.GetElementFD(0x0018,0x9087,dwbValue);

          DCMTKSequence volSeq;
          innerSeq.GetElementSQ(0x0018, 0x9076,volSeq);
          DCMTKSequence innerSeq2;
          volSeq.GetSequence(0,innerSeq2);
          double *dwgVal;
          innerSeq2.GetElementFD(0x0018,0x9089,dwgVal);
          std::vector<double> v(3);
          v[0] = dwgVal[0];
          v[1] = dwgVal[1];
          v[2] = dwgVal[2];
          unsigned int nOld = gradientDirectionAndBValue.size();
          gradientDirectionAndBValue[v] = dwbValue;
          unsigned int nNew = gradientDirectionAndBValue.size();

          if (nOld != nNew)
            {
            vnl_vector_fixed<double, 3> vect3d;
            vect3d[0] = v[0]; vect3d[1] = v[1]; vect3d[2] = v[2];
            UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.push_back(vect3d);
            // vect3d.normalize();
            DiffusionVectors.push_back( vect3d );

            bValues.push_back( dwbValue);
            }
          }
        }

      numberOfSlicesPerVolume=sliceLocations.size();

      std::cout << "LPS Matrix: " << std::endl << LPSDirCos << std::endl;
      std::cout << "Volume Origin: " << std::endl << ImageOrigin[0] << ","
                << ImageOrigin[1] << ","  << ImageOrigin[2] << "," << std::endl;
      std::cout << "Number of slices per volume: " << numberOfSlicesPerVolume << std::endl;
      std::cout << "Slice matrix size: " << nRows << " X " << nCols << std::endl;
      std::cout << "Image resolution: " << xRes << ", " << yRes << ", " << sliceSpacing << std::endl;

      NRRDSpaceDirection=LPSDirCos*OrientationMatrix*SpacingMatrix;

      MeasurementFrame=LPSDirCos;

      nSliceInVolume = sliceLocations.size();
      nVolume = nItems/nSliceInVolume;
      nIgnoreVolume = ignorePhilipsSliceMultiFrame.size()/nSliceInVolume;

      for( unsigned int k2 = 0; k2 < bValues.size(); k2++ )
        {
        std::cout << k2 << ": direction: " <<  DiffusionVectors[k2][0]
                  << ", " << DiffusionVectors[k2][1] << ", " << DiffusionVectors[k2][2]
                  << ", b-value: " << bValues[k2] << std::endl;
        }

      }
    else
      {
      std::cout << "ERROR: Unknown scanner vendor " << vendor << std::endl;
      std::cout << "       this dti file format is properly handled." << std::endl;
      return EXIT_FAILURE;
      }

    ///////////////////////////////////////////////
    // write volumes in raw format
    itk::ImageFileWriter< VolumeType >::Pointer rawWriter = itk::ImageFileWriter< VolumeType >::New();
    itk::RawImageIO<PixelValueType, 3>::Pointer rawIO = itk::RawImageIO<PixelValueType, 3>::New();
    //std::string rawFileName = outputDir + "/" + dataname;
    if ( !nrrdFormat )
      {
      rawWriter->SetFileName( outputVolumeDataName.c_str() );
      rawWriter->SetImageIO( rawIO );
      rawIO->SetByteOrderToLittleEndian();
      }

    // imgWriter is used to write out image in case it is not a dicom DWI image
    itk::ImageFileWriter< VolumeType >::Pointer imgWriter = itk::ImageFileWriter< VolumeType >::New();

    ///////////////////////////////////////////////
    // Update the number of volumes based on the
    // number to ignore from the header information
    const unsigned int nUsableVolumes = nVolume-nIgnoreVolume-bad_gradient_indices.size();
    std::cout << "Number of usable volumes: " << nUsableVolumes << std::endl;

    if ( vendor.find("GE") != std::string::npos ||
         (vendor.find("SIEMENS") != std::string::npos && !SliceMosaic) )
      {
      if (nUsableVolumes == 1)
        {
        imgWriter->SetInput( reader->GetOutput() );
        imgWriter->SetFileName( outputVolumeHeaderName.c_str() );
        try
          {
          imgWriter->Update();
          }
        catch (itk::ExceptionObject &excp)
          {
          std::cerr << "Exception thrown while reading the series" << std::endl;
          std::cerr << excp << std::endl;
          return EXIT_FAILURE;
          }
        return EXIT_SUCCESS;
        }
      else
        {
        if ( !nrrdFormat )
          {
          rawWriter->SetInput( reader->GetOutput() );
          try
            {
            rawWriter->Update();
            }
          catch (itk::ExceptionObject &excp)
            {
            std::cerr << "Exception thrown while reading the series" << std::endl;
            std::cerr << excp << std::endl;
            return EXIT_FAILURE;
            }
          }
        }
      }
    else if ( vendor.find("SIEMENS") != std::string::npos && SliceMosaic)
      {
      // de-mosaic
      nRows /= mMosaic;
      nCols /= nMosaic;

      // center the volume since the image position patient given in the
      // dicom header was useless
      ImageOrigin[0] = -(nRows*(NRRDSpaceDirection[0][0]) +
                         nCols*(NRRDSpaceDirection[0][1]) +
                         nSliceInVolume*(NRRDSpaceDirection[0][2]))/2.0;
      ImageOrigin[1] = -(nRows*(NRRDSpaceDirection[1][0]) +
                         nCols*(NRRDSpaceDirection[1][1]) +
                         nSliceInVolume*(NRRDSpaceDirection[1][2]))/2.0;
      ImageOrigin[2] = -(nRows*(NRRDSpaceDirection[2][0]) +
                         nCols*(NRRDSpaceDirection[2][1]) +
                         nSliceInVolume*(NRRDSpaceDirection[2][2]))/2.0;

      VolumeType::Pointer img = reader->GetOutput();

      VolumeType::RegionType region = img->GetLargestPossibleRegion();
      VolumeType::SizeType size = region.GetSize();

      VolumeType::SizeType dmSize = size;
      unsigned int original_slice_number = dmSize[2] * nSliceInVolume;
      dmSize[0] /= mMosaic;
      dmSize[1] /= nMosaic;
      dmSize[2] = nUsableVolumes * nSliceInVolume;

      region.SetSize( dmSize );
      dmImage->CopyInformation( img );
      dmImage->SetRegions( region );
      dmImage->Allocate();

      VolumeType::RegionType dmRegion = dmImage->GetLargestPossibleRegion();
      dmRegion.SetSize(2, 1);
      region.SetSize(0, dmSize[0]);
      region.SetSize(1, dmSize[1]);
      region.SetSize(2, 1);

      //    int rawMosaic = 0;
      //    int colMosaic = 0;

      bool bad_slice = false;
      unsigned int bad_slice_counter = 0;
      for (unsigned int k = 0; k < original_slice_number; k++)
        {
        for ( unsigned int j = 0; j < bad_gradient_indices.size(); j++)
          {
          unsigned int start_bad_slice_number = bad_gradient_indices[j] * nSliceInVolume;
          unsigned int end_bad_slice_number = start_bad_slice_number + (nSliceInVolume - 1);

          if (k >= start_bad_slice_number && k <= end_bad_slice_number)
            {
            bad_slice = true;
            bad_slice_counter++;
            break;
            }
          else
            {
            bad_slice = false;
            }
          }

        if (bad_slice == false)
          {
          unsigned int new_k = k - bad_slice_counter;

          dmRegion.SetIndex(2, new_k);
          itk::ImageRegionIteratorWithIndex<VolumeType> dmIt( dmImage, dmRegion );

          // figure out the mosaic region for this slice
          int sliceIndex = k;

          //int nBlockPerSlice = mMosaic*nMosaic;
          int slcMosaic = sliceIndex/(nSliceInVolume);
          sliceIndex -= slcMosaic*nSliceInVolume;
          int colMosaic = sliceIndex/mMosaic;
          int rawMosaic = sliceIndex - mMosaic*colMosaic;
          region.SetIndex( 0, rawMosaic*dmSize[0] );
          region.SetIndex( 1, colMosaic*dmSize[1] );
          region.SetIndex( 2, slcMosaic );

          itk::ImageRegionConstIteratorWithIndex<VolumeType> imIt( img, region );
          for ( dmIt.GoToBegin(), imIt.GoToBegin(); !dmIt.IsAtEnd(); ++dmIt, ++imIt)
            {
            dmIt.Set( imIt.Get() );
            }
          }
        }

      if (nUsableVolumes == 1)
        {
        imgWriter->SetInput( dmImage );
        imgWriter->SetFileName( outputVolumeHeaderName.c_str() );
        try
          {
          imgWriter->Update();
          }
        catch (itk::ExceptionObject &excp)
          {
          std::cerr << "Exception thrown while reading the series" << std::endl;
          std::cerr << excp << std::endl;
          return EXIT_FAILURE;
          }
        return EXIT_SUCCESS;
        }
      else
        {
        if ( !nrrdFormat )
          {
          rawWriter->SetInput( dmImage );
          try
            {
            rawWriter->Update();
            }
          catch (itk::ExceptionObject &excp)
            {
            std::cerr << "Exception thrown while reading the series" << std::endl;
            std::cerr << excp << std::endl;
            return EXIT_FAILURE;
            }
          }
        }
      }
    else if (vendor.find("PHILIPS") != std::string::npos)
      {
      VolumeType::Pointer img = reader->GetOutput();

      VolumeType::RegionType region = img->GetLargestPossibleRegion();
      VolumeType::SizeType size = region.GetSize();

      VolumeType::SizeType dmSize = size;
      dmSize[2] = nSliceInVolume * (nUsableVolumes);

      region.SetSize( dmSize );
      dmImage->CopyInformation( img );
      dmImage->SetRegions( region );
      dmImage->Allocate();

      VolumeType::RegionType dmRegion = dmImage->GetLargestPossibleRegion();
      dmRegion.SetSize(2, 1);
      region.SetSize(0, dmSize[0]);
      region.SetSize(1, dmSize[1]);
      region.SetSize(2, 1);

      unsigned int count = 0;
      for (unsigned int i = 0; i < nVolume; i++)
        {
        if (useVolume[i] == 1)
          {
          for (unsigned int k = 0; k < nSliceInVolume; k++)
            {
            dmRegion.SetIndex(0, 0);
            dmRegion.SetIndex(1, 0);
            dmRegion.SetIndex(2, count*(nSliceInVolume)+k);
            itk::ImageRegionIteratorWithIndex<VolumeType> dmIt( dmImage, dmRegion );

            // figure out the region for this slice
            const int sliceIndex = k*nVolume+i;
            region.SetIndex( 0, 0 );
            region.SetIndex( 1, 0 );
            region.SetIndex( 2, sliceIndex );

            itk::ImageRegionConstIteratorWithIndex<VolumeType> imIt( img, region );

            for ( dmIt.GoToBegin(), imIt.GoToBegin(); !dmIt.IsAtEnd(); ++dmIt, ++imIt)
              {
              dmIt.Set( imIt.Get() );
              }
            }
          count++;
          }
        }
      if (nUsableVolumes == 1)
        {
        imgWriter->SetInput( dmImage );
        imgWriter->SetFileName( outputVolumeHeaderName.c_str() );
        try
          {
          imgWriter->Update();
          }
        catch (itk::ExceptionObject &excp)
          {
          std::cerr << "Exception thrown while reading the series" << std::endl;
          std::cerr << excp << std::endl;
          return EXIT_FAILURE;
          }
        return EXIT_SUCCESS;
        }
      else
        {
        if ( !nrrdFormat )
          {
          rawWriter->SetInput( reader->GetOutput() );
          try
            {
            rawWriter->Update();
            }
          catch (itk::ExceptionObject &excp)
            {
            std::cerr << "Exception thrown while reading the series" << std::endl;
            std::cerr << excp << std::endl;
            return EXIT_FAILURE;
            }
          }
        }
      //Verify sizes
      if( count != bValues.size() )
        {
        std::cout << "ERROR:  bValues are the wrong size." <<  count << " != " << bValues.size() << std::endl;
        return EXIT_FAILURE;
        }
      if( count != DiffusionVectors.size() )
        {
        std::cout << "ERROR:  DiffusionVectors are the wrong size." <<  count << " != " << DiffusionVectors.size() << std::endl;
        return EXIT_FAILURE;
        }
      if( count != UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.size() )
        {
        std::cout << "ERROR:  UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem are the wrong size."
                  <<  count << " != " << UnmodifiedDiffusionVectorsInDicomLPSCoordinateSystem.size() << std::endl;
        return EXIT_FAILURE;
        }
      }
    else
      {
      std::cout << "Warning:  invalid vendor found." << std::endl;
      WriteVolume( reader->GetOutput(), outputVolumeHeaderName );
      return EXIT_SUCCESS;
      }


    const vnl_matrix_fixed<double,3,3> InverseMeasurementFrame= MeasurementFrame.GetInverse();
    {
    //////////////////////////////////////////////
    // write header file
    // This part follows a DWI NRRD file in NRRD format 5.
    // There should be a better way using itkNRRDImageIO.

    std::ofstream header;
    //std::string headerFileName = outputDir + "/" + outputFileName;

    header.open (outputVolumeHeaderName.c_str(), std::ios::out | std::ios::binary);
    header << "NRRD0005" << std::endl;

    if (!nrrdFormat)
      {
      header << "content: exists(" << itksys::SystemTools::GetFilenameName(outputVolumeDataName) << ",0)" << std::endl;
      }
    header << "type: short" << std::endl;
    header << "dimension: 4" << std::endl;

    // need to check
    header << "space: " << nrrdSpaceDefinition << "" << std::endl;
    // in nrrd, size array is the number of pixels in 1st, 2nd, 3rd, ... dimensions
    header << "sizes: " << nCols << " " << nRows << " " << nSliceInVolume << " " << nUsableVolumes << std::endl;
    header << "thicknesses:  NaN  NaN " << sliceSpacing << " NaN" << std::endl;

    // need to check
    header << "space directions: "
           << "(" << (NRRDSpaceDirection[0][0]) << ","<< (NRRDSpaceDirection[1][0]) << ","<< (NRRDSpaceDirection[2][0]) << ") "
           << "(" << (NRRDSpaceDirection[0][1]) << ","<< (NRRDSpaceDirection[1][1]) << ","<< (NRRDSpaceDirection[2][1]) << ") "
           << "(" << (NRRDSpaceDirection[0][2]) << ","<< (NRRDSpaceDirection[1][2]) << ","<< (NRRDSpaceDirection[2][2])
           << ") none" << std::endl;
    header << "centerings: cell cell cell ???" << std::endl;
    header << "kinds: space space space list" << std::endl;

    header << "endian: little" << std::endl;
    header << "encoding: raw" << std::endl;
    header << "space units: \"mm\" \"mm\" \"mm\"" << std::endl;
    header << "space origin: "
           <<"(" << ImageOrigin[0] << ","<< ImageOrigin[1] << ","<< ImageOrigin[2] << ") " << std::endl;
    if (!nrrdFormat)
      {
      header << "data file: " << itksys::SystemTools::GetFilenameName(outputVolumeDataName) << std::endl;
      }

    // For scanners, the measurement frame for the gradient directions is the same as the
    // Excerpt from http://teem.sourceforge.net/nrrd/format.html definition of "measurement frame:"
    // There is also the possibility that a measurement frame
    // should be recorded for an image even though it is storing
    // only scalar values (e.g., a sequence of diffusion-weighted MR
    // images has a measurement frame for the coefficients of
    // the diffusion-sensitizing gradient directions, and
    // the measurement frame field is the logical store
    // this information).
    //
    // It was noticed on oblique Philips DTI scans that the prescribed protocol directions were
    // rotated by the ImageOrientationPatient amount and recorded in the DICOM header.
    // In order to compare two different scans to determine if the same protocol was prosribed,
    // it is necessary to multiply each of the recorded diffusion gradient directions by
    // the inverse of the LPSDirCos.
    if(useIdentityMeaseurementFrame)
      {
      header << "measurement frame: "
             << "(" << 1 << ","<< 0 << ","<< 0 << ") "
             << "(" << 0 << ","<< 1 << ","<< 0 << ") "
             << "(" << 0 << ","<< 0 << ","<< 1 << ")"
             << std::endl;
      }
    else
      {
      header << "measurement frame: "
             << "(" << (MeasurementFrame[0][0]) << ","<< (MeasurementFrame[1][0]) << ","<< (MeasurementFrame[2][0]) << ") "
             << "(" << (MeasurementFrame[0][1]) << ","<< (MeasurementFrame[1][1]) << ","<< (MeasurementFrame[2][1]) << ") "
             << "(" << (MeasurementFrame[0][2]) << ","<< (MeasurementFrame[1][2]) << ","<< (MeasurementFrame[2][2]) << ")"
             << std::endl;
      }

    header << "modality:=DWMRI" << std::endl;
    //  float bValue = 0;
    for (unsigned int k = 0; k < nUsableVolumes; k++)
      {
      if (bValues[k] > maxBvalue)
        {
        maxBvalue = bValues[k];
        }
      }

    // this is the norminal BValue, i.e. the largest one.
    header << "DWMRI_b-value:=" << maxBvalue << std::endl;

    //  the following three lines are for older NRRD format, where
    //  baseline images are always in the begining.
    //  header << "DWMRI_gradient_0000:=0  0  0" << std::endl;
    //  header << "DWMRI_NEX_0000:=" << nBaseline << std::endl;
    //  need to check

    unsigned int shift_index = 0;
    unsigned int original_volume_number = nUsableVolumes + bad_gradient_indices.size();

    for (unsigned int k = 0; k < original_volume_number; k++)
      {
      float scaleFactor = 0;
      bool print_gradient = true;

      for (unsigned int j = 0; j < bad_gradient_indices.size(); j++)
        {
        if (k == bad_gradient_indices[j])
          {
          shift_index++;
          print_gradient = false;
          continue;
          }
        }

      if (maxBvalue > 0)
        {
        scaleFactor = sqrt( bValues[k]/maxBvalue );
        }
      std::cout << "For Multiple BValues: " << k << " -- " << bValues[k] << " / " << maxBvalue << " = " << scaleFactor << std::endl;

      if (print_gradient == true)
        {
        if(useIdentityMeaseurementFrame)
          {
          vnl_vector_fixed<double,3> RotatedDiffusionVectors=InverseMeasurementFrame*(DiffusionVectors[k-nBaseline]);
          header << "DWMRI_gradient_" << std::setw(4) << std::setfill('0') << k << ":="
                 << RotatedDiffusionVectors[0] * scaleFactor << "   "
                 << RotatedDiffusionVectors[1] * scaleFactor << "   "
                 << RotatedDiffusionVectors[2] * scaleFactor << std::endl;
          }
        else
          {
          if(useBMatrixGradientDirections)
            {
            header << "DWMRI_gradient_" << std::setw(4) << std::setfill('0') << k << ":="
                   << DiffusionVectors[k][0] << "   "
                   << DiffusionVectors[k][1] << "   "
                   << DiffusionVectors[k][2] << std::endl;
            }
          else
            {
            unsigned int printed_gradient_number = k - shift_index;

            header << "DWMRI_gradient_" << std::setw(4) << std::setfill('0') << printed_gradient_number << ":="
                   << DiffusionVectors[k-nBaseline][0] * scaleFactor << "   "
                   << DiffusionVectors[k-nBaseline][1] * scaleFactor << "   "
                   << DiffusionVectors[k-nBaseline][2] * scaleFactor << std::endl;
            }
          }
        }
      else
        {
        std::cout << "Gradient " << k << " was removed and will not be printed in the NRRD header file." << std::endl;
        }

      //std::cout << "Consistent Orientation Checks." << std::endl;
      //std::cout << "DWMRI_gradient_" << std::setw(4) << std::setfill('0') << k << ":="
      //  << LPSDirCos.GetInverse()*DiffusionVectors[k-nBaseline] << std::endl;
      }

    // write data in the same file is .nrrd was chosen
    header << std::endl;;
    if (nrrdFormat && SliceMosaic)
      {
      unsigned long nVoxels = dmImage->GetBufferedRegion().GetNumberOfPixels();
      header.write( reinterpret_cast<char *>(dmImage->GetBufferPointer()),
                    nVoxels*sizeof(short) );
      }
    else if (nrrdFormat)
      {
      unsigned long nVoxels = reader->GetOutput()->GetBufferedRegion().GetNumberOfPixels();
      header.write( reinterpret_cast<char *>(reader->GetOutput()->GetBufferPointer()),
                    nVoxels*sizeof(short) );
      }

    header.close();
    }

    if( writeProtocolGradientsFile == true )
      {
      //////////////////////////////////////////////
      // writeProtocolGradientsFile write protocolGradientsFile file
      // This part follows a DWI NRRD file in NRRD format 5.
      // There should be a better way using itkNRRDImageIO.

      std::ofstream protocolGradientsFile;
      //std::string protocolGradientsFileFileName = outputDir + "/" + outputFileName;

      const std::string protocolGradientsFileName=outputVolumeHeaderName+".txt";
      protocolGradientsFile.open ( protocolGradientsFileName.c_str() );
      protocolGradientsFile << "ImageOrientationPatient (0020|0032): "
                            << LPSDirCos[0][0] << "\\" << LPSDirCos[1][0] << "\\" << LPSDirCos[2][0] << "\\"
                            << LPSDirCos[0][1] << "\\" << LPSDirCos[1][1] << "\\" << LPSDirCos[2][1] << "\\"
                            << std::endl;
      protocolGradientsFile << "==================================" << std::endl;
      protocolGradientsFile << "Direction Cosines: " << std::endl << LPSDirCos << std::endl;
      protocolGradientsFile << "==================================" << std::endl;
      protocolGradientsFile << "MeasurementFrame: " << std::endl << MeasurementFrame << std::endl;
      protocolGradientsFile << "==================================" << std::endl;
      for (unsigned int k = 0; k < nUsableVolumes; k++)
        {
        float scaleFactor = 0;
        if (maxBvalue > 0)
          {
          scaleFactor = sqrt( bValues[k]/maxBvalue );
          }
        protocolGradientsFile << "DWMRI_gradient_" << std::setw(4) << std::setfill('0') << k << "=["
                              << DiffusionVectors[k-nBaseline][0] * scaleFactor << ";"
                              << DiffusionVectors[k-nBaseline][1] * scaleFactor << ";"
                              << DiffusionVectors[k-nBaseline][2] * scaleFactor << "]" <<std::endl;
        }
      protocolGradientsFile << "==================================" << std::endl;
      for (unsigned int k = 0; k < nUsableVolumes; k++)
        {
        float scaleFactor = 0;
        if (maxBvalue > 0)
          {
          scaleFactor = sqrt( bValues[k]/maxBvalue );
          }
        const vnl_vector_fixed<double, 3u> ProtocolGradient= InverseMeasurementFrame*DiffusionVectors[k-nBaseline];
        protocolGradientsFile << "Protocol_gradient_" << std::setw(4) << std::setfill('0') << k << "=["
                              << ProtocolGradient[0] * scaleFactor << ";"
                              << ProtocolGradient[1] * scaleFactor << ";"
                              << ProtocolGradient[2] * scaleFactor << "]" <<std::endl;
        }
      protocolGradientsFile << "==================================" << std::endl;
      protocolGradientsFile.close();
      }
    }
  catch (itk::ExceptionObject &excp)
    {
    std::cerr << "Exception caught " << excp << std::endl;
    return EXIT_FAILURE;
    }
  return EXIT_SUCCESS;

}
