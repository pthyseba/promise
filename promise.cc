#include "promise.h"
#include "unistd.h"
#include <cmath>
#include <iostream>
#include <cstdlib>

using namespace NPromise;

int main(int argc, char** argv)
{
  Promise<int>::TExecutor e = [](Promise<int>::TResolver aResolver, Promise<int>::TRejecter aRejecter){ sleep(10); std::cout << "thread " << std::this_thread::get_id()<< " awake" << std::endl; aResolver(5);};
  Promise<int>::TExecutor e2 = [](Promise<int>::TResolver aResolver, Promise<int>::TRejecter){ sleep(11); std::cout << "thread " << std::this_thread::get_id() << " awake" << std::endl; aResolver(55);};
  Promise<int> p(e);
  std::function<void(const int&, Promise<double>::TResolver, Promise<double>::TRejecter)> resolveHandler = [](const int& aResult, Promise<double>::TResolver aResolver, Promise<double>::TRejecter aRejecter)
           {
             std::cout << "Resolve handler" << std::endl;
             throw(13);
	     aResolver(sqrt((double)aResult));
           };

  std::function<void(const std::string&, Promise<double>::TResolver, Promise<double>::TRejecter)> rejectHandler = [](const std::string& aReason, Promise<double>::TResolver aResolver, Promise<double>::TRejecter aRejecter)
           {
	     std::cout << "Reject handler" << std::endl;
             aRejecter("Original promise rejected");
           };

  auto p2 = p.then<double>(resolveHandler, rejectHandler);
  //Promise<int> p3(e2);
  sleep(12);
  if (p2.isFulfilled())
  {
    std::cout << "Result: " << p2.GetResult() << std::endl;
  }
  else
  {
    std::cout << "Rejected with reason: " << p2.GetReason() << std::endl;
  }
  //Promise<int> test([](Promise<int>::TResolver aResolve, Promise<int>::TRejecter aReject){sleep(2); aResolve(7);});
  
  std::function<int(const std::tuple<double>&)> firstHandler = 
    [](const std::tuple<double>& a)
    {
      std::cout << "Handler!" << std::endl;
      std::cout << std::get<0>(a) << std::endl;
      //      std::cout << std::get<1>(a) << std::endl;
      return 0;
    };

  sleep(1);
  auto a = All(p2).then(firstHandler);
  //std::cout << "more stuff" << std::endl;
  std::cout << "After all..." << std::endl;

  /*
  Promise<int> p4(e);
  std::function<Promise<double>(const int&)> chainHandler = 
  [](const int& a)
  {
 
    Promise<double>::TExecutor e = 
      [value=a](Promise<double>::TResolver aResolver, Promise<double>::TRejecter)
      {
	std::cout << "chain handler: received " << value << std::endl;
        sleep(3);
        aResolver(sqrt(value));
      };

    return Promise<double>(e);
  };
  
  auto p5 = p4.then(chainHandler);

  sleep(20);
  if (p5.isFulfilled())
    std::cout << "p5 resolved to " << p5.GetResult() << std::endl;
  
  std::cout << "test resolved to " << test.GetResult() << std::endl;
  
  std::function<int(const int&)> handler = [](int aResult){ std::cout << "Handler!" << std::endl; return aResult + 1; };
  Promise<int> p2 = p.then(handler);
 
  
  std::cout << "Waiting.." << std::endl;
  sleep(11);
  
  if (p2.isFulfilled())
  {
    std::cout << p2.GetResult() << std::endl;
  } 
  else
  {
    std::cout << p2.GetReason() << std::endl;
  }
  */
 
}
